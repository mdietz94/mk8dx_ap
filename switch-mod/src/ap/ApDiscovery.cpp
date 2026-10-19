// See ApDiscovery.hpp for the design rationale.
//
// Probe sequence (the dread_ap scheme):
//   1. Loopback 127.0.0.1 — covers Ryujinx-on-same-host. Fast (250ms) and
//      the most common dev path, so we try it first.
//   2. Unicast sweep across the /24 of the PC address in ap_config.json
//      (written next to the mod by the setup wizard, read once at
//      nn::fs::MountRom time by RomConfigHook). All 254 hosts get one probe
//      in a tight burst (~5ms send), then we poll for up to 1s for any
//      reply. First valid {"t":"bridge"} wins. A 255.255.255.255 broadcast
//      is silently dropped on a lot of real-world LANs (travel routers, mesh
//      repeaters, managed switches), hence the unicast sweep. A missing
//      config means loopback only.
//
// The console's own address is deliberately NOT queried (nifm /
// getsockname): the deploy-time seed is what dread_ap ships on real
// hardware, and nothing about the player's network is compiled in.
//
// Talks to bsd:u via nn::socket::* directly (same session ApClient uses).

#include "ApDiscovery.hpp"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "hk/types.h"

#include "../util/Json.hpp"
#include "../util/Log.hpp"
#include "ApConfig.hpp"      // configuredBridgeHost
#include "ApProtocol.hpp"    // MK8AP_MOD_VERSION_STRING
#include "ApState.hpp"       // ApState::nowMs
#include "ApSubnet.hpp"

// Match the layout / declarations ApClient.cpp uses: the mangled names the
// loader resolves encode `sockaddr` / `in_addr`, so the structs MUST carry
// those names at file scope.
struct in_addr { u32 s_addr; };
struct sockaddr {
    u8 sa_len;
    u8 sa_family;
    u16 sa_port;
    in_addr sa_addr;
    u8 sa_zero[8];
};
struct pollfd { s32 fd; short events; short revents; };

namespace nn::socket {
    s32 Socket(s32, s32, s32);
    s32 SendTo(s32, const void*, unsigned long, s32, const ::sockaddr*, u32);
    s32 RecvFrom(s32, void*, unsigned long, s32, ::sockaddr*, u32*);
    u32 Close(s32);
    s32 Poll(::pollfd*, unsigned long, s32);
    u16 InetHtons(u16);
    s32 InetAton(const char*, ::in_addr*);
    s32 GetLastErrno();
}

namespace mk8ap::ap {

namespace {

using subnet::SweepRange;
using subnet::byteswap32;

// Socket constants. nn::socket doesn't re-export BSD constants; values match
// Nintendo's bsd:u service (FreeBSD-derived). Mirrors ApClient.cpp.
constexpr s32 kAfInet      = 2;
constexpr s32 kSockDgram   = 2;
constexpr s32 kIpprotoUdp  = 17;
constexpr s32 kPollIn      = 0x0001;

// Probe timeouts.
constexpr std::uint32_t kSweepCollectMs   = 1000;  // wait window after the burst
constexpr std::uint32_t kLoopbackProbeMs  = 250;

// Reply buffer cap. Replies are tiny (~80 bytes); 512 is generous.
constexpr std::size_t kReplyBufBytes = 512;

// Diagnostic report, spinlock-guarded. Writer: whatever worker thread last
// called resolveBridge(). Reader: any thread (debug overlay, log dump).
DiscoveryReport         g_report;
std::atomic_flag        g_report_lock = ATOMIC_FLAG_INIT;

struct ReportGuard {
    ReportGuard()  { while (g_report_lock.test_and_set(std::memory_order_acquire)) {} }
    ~ReportGuard() { g_report_lock.clear(std::memory_order_release); }
};

void publishReport(const DiscoveryReport& src) {
    ReportGuard g;
    g_report = src;
}

// Build the probe payload once per resolveBridge call.
std::size_t buildProbe(char* dst, std::size_t cap) {
    mk8ap::util::json::LineBuffer line;
    mk8ap::util::json::Encoder e{line};
    e.beginObject()
        .key("t").value("discover")
        .key("mod_ver").value(MK8AP_MOD_VERSION_STRING)
     .endObject();
    line.append('\n');
    const std::size_t take = line.size() < cap ? line.size() : cap;
    std::memcpy(dst, line.data(), take);
    return take;
}

s32 openUdpSocket() {
    return nn::socket::Socket(kAfInet, kSockDgram, kIpprotoUdp);
}

void closeSocket(s32 fd) {
    (void)nn::socket::Close(fd);
}

bool waitReadable(s32 fd, std::uint32_t timeout_ms) {
    ::pollfd pfd{ .fd = fd, .events = kPollIn, .revents = 0 };
    const s32 n = nn::socket::Poll(&pfd, 1, static_cast<s32>(timeout_ms));
    if (n <= 0) return false;
    return (pfd.revents & kPollIn) != 0;
}

bool parseReply(const char* data, std::size_t len, BridgeTarget& out) {
    char scratch[kReplyBufBytes];
    if (len > sizeof(scratch)) len = sizeof(scratch);
    std::memcpy(scratch, data, len);

    mk8ap::util::json::Reader r(scratch, len);
    if (!r.enterObject()) return false;

    bool saw_t_bridge = false;
    char host[64] = {0};
    int port = 0;

    std::string_view key;
    while (r.nextField(key)) {
        if (key == "t") {
            std::string_view t_val;
            if (!r.nextString(t_val)) return false;
            if (t_val == "bridge") saw_t_bridge = true;
        } else if (key == "host") {
            std::string_view host_val;
            if (!r.nextString(host_val)) return false;
            const std::size_t take = host_val.size() < sizeof(host) - 1
                ? host_val.size() : sizeof(host) - 1;
            std::memcpy(host, host_val.data(), take);
            host[take] = '\0';
        } else if (key == "port") {
            std::int64_t p = 0;
            if (!r.nextInt(p)) return false;
            port = static_cast<int>(p);
        } else {
            std::string_view _sv;
            std::int64_t _i;
            bool _b;
            (void)(r.isNull() || r.nextString(_sv) || r.nextInt(_i) || r.nextBool(_b));
        }
    }
    if (!saw_t_bridge || host[0] == '\0' || port <= 0 || port > 0xFFFF) {
        return false;
    }
    out.host = host;
    out.port = static_cast<std::uint16_t>(port);
    return true;
}

void makeSockAddrFromU32(u32 host_nbo, std::uint16_t port, ::sockaddr& out) {
    out = ::sockaddr{};
    out.sa_len    = sizeof(out);
    out.sa_family = static_cast<u8>(kAfInet);
    out.sa_port   = nn::socket::InetHtons(port);
    out.sa_addr.s_addr = host_nbo;
}

bool oneProbeLiteral(s32 fd, const char* probe_data, std::size_t probe_len,
                     const char* host, std::uint16_t port,
                     std::uint32_t timeout_ms, BridgeTarget& out) {
    ::in_addr ia{};
    if (nn::socket::InetAton(host, &ia) == 0) {
        MK8AP_LOG_WARN("[discover] InetAton failed for %s", host);
        return false;
    }
    ::sockaddr addr{};
    addr.sa_len = sizeof(addr);
    addr.sa_family = static_cast<u8>(kAfInet);
    addr.sa_port = nn::socket::InetHtons(port);
    addr.sa_addr = ia;

    const s32 sent = nn::socket::SendTo(
        fd, probe_data, probe_len, 0, &addr, sizeof(addr));
    if (sent < 0) {
        MK8AP_LOG_WARN("[discover] sendTo %s:%u failed errno=%d",
                       host, port, nn::socket::GetLastErrno());
        return false;
    }
    if (!waitReadable(fd, timeout_ms)) return false;
    char buf[kReplyBufBytes];
    ::sockaddr from{};
    u32 from_len = sizeof(from);
    const s32 got = nn::socket::RecvFrom(
        fd, buf, sizeof(buf), 0, &from, &from_len);
    if (got <= 0) return false;
    return parseReply(buf, static_cast<std::size_t>(got), out);
}

// ---- Sweep ---------------------------------------------------------------

// Burst-send one probe to every host of every range, then drain inbound
// replies for up to kSweepCollectMs. First valid reply wins.
bool sweep(s32 fd, const char* probe_data, std::size_t probe_len,
           const SweepRange* ranges, int range_count, std::uint16_t port,
           DiscoveryReport& report_out, BridgeTarget& out) {
    int sent_count = 0;
    for (int i = 0; i < range_count; ++i) {
        const SweepRange& r = ranges[i];
        for (std::uint32_t ip = r.first; !r.empty() && ip <= r.last; ++ip) {
            ::sockaddr addr{};
            makeSockAddrFromU32(byteswap32(ip), port, addr);
            const s32 n = nn::socket::SendTo(
                fd, probe_data, probe_len, 0, &addr, sizeof(addr));
            if (n >= 0) ++sent_count;
            // Per-host failures (errno=ENETUNREACH) aren't surprising on
            // sparse LANs; aggregate stats land in the report.
        }
        MK8AP_LOG_INFO("[discover] sweep %u.%u.%u.%u-%u.%u.%u.%u",
                       (r.first >> 24) & 0xFF, (r.first >> 16) & 0xFF,
                       (r.first >> 8) & 0xFF, r.first & 0xFF,
                       (r.last >> 24) & 0xFF, (r.last >> 16) & 0xFF,
                       (r.last >> 8) & 0xFF, r.last & 0xFF);
    }
    report_out.probed_count = static_cast<std::uint16_t>(sent_count);
    MK8AP_LOG_INFO("[discover] sweep sent %d probes", sent_count);

    const std::int64_t deadline_ms = ApState::nowMs() + kSweepCollectMs;
    while (ApState::nowMs() < deadline_ms) {
        const std::int64_t remain = deadline_ms - ApState::nowMs();
        const std::uint32_t wait_ms =
            (remain > 0) ? static_cast<std::uint32_t>(remain) : 1u;
        if (!waitReadable(fd, wait_ms)) break;
        char buf[kReplyBufBytes];
        ::sockaddr from{};
        u32 from_len = sizeof(from);
        const s32 got = nn::socket::RecvFrom(
            fd, buf, sizeof(buf), 0, &from, &from_len);
        if (got <= 0) continue;
        ++report_out.replies;
        BridgeTarget t;
        if (parseReply(buf, static_cast<std::size_t>(got), t)) {
            out = t;
            return true;
        }
    }
    return false;
}

void recordSuccess(DiscoveryReport& report, const BridgeTarget& t) {
    std::snprintf(report.last_bridge_host, sizeof(report.last_bridge_host),
                  "%s", t.host.c_str());
    report.last_bridge_port = t.port;
    report.last_success_ms = ApState::nowMs();
    publishReport(report);
}

}  // namespace

void snapshotDiscoveryReport(DiscoveryReport& out) {
    ReportGuard g;
    out = g_report;
}

bool resolveBridge(BridgeTarget& out, std::uint16_t discovery_port) {
    char probe[kReplyBufBytes];
    const std::size_t probe_len = buildProbe(probe, sizeof(probe));
    if (probe_len == 0) return false;

    DiscoveryReport report{};
    report.last_attempt_ms = ApState::nowMs();

    // ---- Step 1: loopback (Ryujinx-on-same-host) ----
    {
        const s32 fd = openUdpSocket();
        if (fd >= 0) {
            BridgeTarget t;
            const bool ok = oneProbeLiteral(
                fd, probe, probe_len,
                "127.0.0.1", discovery_port,
                kLoopbackProbeMs, t);
            closeSocket(fd);
            if (ok) {
                report.loopback_used = true;
                recordSuccess(report, t);
                MK8AP_LOG_INFO("[discover] resolved via loopback -> %s:%u",
                               t.host.c_str(), t.port);
                out = t;
                return true;
            }
        } else {
            MK8AP_LOG_WARN("[discover] UDP socket() failed (loopback step)");
        }
    }

    // ---- Step 2: sweep the /24 of the ap_config.json seed ----
    char seed_host[64];
    ::in_addr seed_ia{};
    if (!configuredBridgeHost(seed_host, sizeof(seed_host))) {
        publishReport(report);
        MK8AP_LOG_INFO("[discover] loopback silent and no ap_config.json seed "
                       "(run /setup in the MK8DX Client); retrying with backoff");
        return false;
    }
    if (nn::socket::InetAton(seed_host, &seed_ia) == 0) {
        publishReport(report);
        MK8AP_LOG_WARN("[discover] ap_config.json bridge_host '%s' is not an IPv4 literal",
                       seed_host);
        return false;
    }
    const std::uint32_t seed = byteswap32(seed_ia.s_addr);
    report.self_ip = seed;
    report.subnet_mask = subnet::kMask24;
    const SweepRange ranges[1] = {subnet::sweepRange(seed, subnet::kMask24)};
    if (ranges[0].empty()) {
        publishReport(report);
        MK8AP_LOG_WARN("[discover] ap_config.json bridge_host '%s' is not a sweepable address",
                       seed_host);
        return false;
    }
    MK8AP_LOG_INFO("[discover] sweep seed %s (ap_config.json)", seed_host);
    const int range_count = 1;

    const s32 fd = openUdpSocket();
    if (fd < 0) {
        MK8AP_LOG_WARN("[discover] UDP socket() failed (sweep step)");
        publishReport(report);
        return false;
    }
    BridgeTarget t;
    const bool ok = sweep(fd, probe, probe_len, ranges, range_count,
                          discovery_port, report, t);
    closeSocket(fd);
    if (ok) {
        recordSuccess(report, t);
        MK8AP_LOG_INFO("[discover] resolved via subnet sweep -> %s:%u",
                       t.host.c_str(), t.port);
        out = t;
        return true;
    }

    publishReport(report);
    MK8AP_LOG_INFO("[discover] no UDP reply (loopback + sweep); caller will retry with backoff");
    return false;
}

}  // namespace mk8ap::ap
