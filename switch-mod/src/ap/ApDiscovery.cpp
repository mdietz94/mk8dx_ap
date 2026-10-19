// See ApDiscovery.hpp for the design rationale.
//
// Probe sequence:
//   1. Loopback 127.0.0.1 — covers Ryujinx-on-same-host. Fast (250ms) and
//      the most common dev path, so we try it first.
//   2. Unicast sweep across the console's OWN subnet, learned at runtime
//      (see querySelfAddress), plus the /24 of BRIDGE_HOST_STRING when a
//      developer baked one in and it is a different network. All hosts get
//      one probe in a tight burst (~5ms send), then we poll for up to 1s
//      for any reply. First valid {"t":"bridge"} wins. A 255.255.255.255
//      broadcast is silently dropped on a lot of real-world LANs (travel
//      routers, mesh repeaters, managed switches), hence the unicast sweep.
//
// Learning the console's own address is what lets ONE prebuilt binary
// work on every network: nothing about the player's LAN is compiled in.
// The new SDK symbols are resolved with hk::ro::lookupSymbol (nullptr on a
// miss) and NEVER declared as plain externs: an unresolved import kills
// module init silently (smo_archipelago lost a session to exactly that
// with nn::nifm::GetCurrentPrimaryIpAddress).
//
// Talks to bsd:u via nn::socket::* directly (same session ApClient uses).

#include "ApDiscovery.hpp"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "hk/ro/RoUtil.h"
#include "hk/types.h"

#include "../util/Json.hpp"
#include "../util/Log.hpp"
#include "ApConfig.hpp"      // BRIDGE_HOST_STRING
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
    u32 Connect(s32, const ::sockaddr*, u32);
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

// ---- The console's own address -------------------------------------------

enum class SelfSource : std::uint8_t { None, NifmConfig, NifmPrimary, SockName };

const char* selfSourceName(SelfSource s) {
    switch (s) {
        case SelfSource::NifmConfig:  return "nifm GetCurrentIpConfigInfo";
        case SelfSource::NifmPrimary: return "nifm GetCurrentPrimaryIpAddress";
        case SelfSource::SockName:    return "socket GetSockName";
        default:                      return "none";
    }
}

struct SelfAddress {
    std::uint32_t ip   = 0;  // host order
    std::uint32_t mask = 0;  // host order; 0 = unknown (sweep assumes /24)
    SelfSource    src  = SelfSource::None;
};

// nn::Result is a trivially copyable u32 wrapper: returned in w0, 0 = success.
using IpConfigInfoFn = u32 (*)(::in_addr*, ::in_addr*, ::in_addr*, ::in_addr*, ::in_addr*);
using PrimaryIpFn    = u32 (*)(::in_addr*);
using GetSockNameFn  = s32 (*)(s32, ::sockaddr*, u32*);

struct SelfSyms {
    bool           looked_up = false;
    IpConfigInfoFn ip_config = nullptr;
    PrimaryIpFn    primary   = nullptr;
    GetSockNameFn  sockname  = nullptr;
};
SelfSyms g_self_syms;

// SDK 4.4.0 spells the address type `in_addr`; later SDKs moved it to
// nn::socket::InAddr / SockAddr. Try both manglings. SdkSymbolDump logs the
// names the running game actually exports.
::ptr lookupFirst(const char* a, const char* b) {
    ::ptr p = hk::ro::lookupSymbol(a);
    return p ? p : hk::ro::lookupSymbol(b);
}

void lookupSelfSyms() {
    if (g_self_syms.looked_up) return;
    g_self_syms.looked_up = true;
    g_self_syms.ip_config = reinterpret_cast<IpConfigInfoFn>(lookupFirst(
        "_ZN2nn4nifm22GetCurrentIpConfigInfoEP7in_addrS2_S2_S2_S2_",
        "_ZN2nn4nifm22GetCurrentIpConfigInfoEPNS_6socket6InAddrES3_S3_S3_S3_"));
    g_self_syms.primary = reinterpret_cast<PrimaryIpFn>(lookupFirst(
        "_ZN2nn4nifm26GetCurrentPrimaryIpAddressEP7in_addr",
        "_ZN2nn4nifm26GetCurrentPrimaryIpAddressEPNS_6socket6InAddrE"));
    g_self_syms.sockname = reinterpret_cast<GetSockNameFn>(lookupFirst(
        "_ZN2nn6socket11GetSockNameEiP8sockaddrPj",
        "_ZN2nn6socket11GetSockNameEiPNS0_8SockAddrEPj"));
    MK8AP_LOG_INFO("[discover] self-address syms: ipconfig=%s primary=%s sockname=%s",
                   g_self_syms.ip_config ? "yes" : "NO",
                   g_self_syms.primary ? "yes" : "NO",
                   g_self_syms.sockname ? "yes" : "NO");
}

// Connect a UDP socket to a public address and read back the local address
// the routing table picked. A UDP connect sends no packet; it only needs a
// default route, which every LAN with a router has.
std::uint32_t selfIpFromSockName() {
    if (!g_self_syms.sockname) return 0;
    const s32 fd = openUdpSocket();
    if (fd < 0) return 0;
    ::sockaddr dst{};
    makeSockAddrFromU32(byteswap32(0x08080808u), 53, dst);  // 8.8.8.8:53
    std::uint32_t ip = 0;
    if (nn::socket::Connect(fd, &dst, sizeof(dst)) == 0) {
        ::sockaddr local{};
        u32 len = sizeof(local);
        if (g_self_syms.sockname(fd, &local, &len) == 0) {
            ip = byteswap32(local.sa_addr.s_addr);
        }
    }
    closeSocket(fd);
    return ip;
}

SelfAddress querySelfAddress() {
    lookupSelfSyms();
    SelfAddress s;
    if (g_self_syms.ip_config) {
        ::in_addr ip{}, mask{}, gw{}, dns1{}, dns2{};
        if (g_self_syms.ip_config(&ip, &mask, &gw, &dns1, &dns2) == 0 &&
            subnet::isUsableHost(byteswap32(ip.s_addr))) {
            s.ip   = byteswap32(ip.s_addr);
            s.mask = byteswap32(mask.s_addr);
            s.src  = SelfSource::NifmConfig;
            return s;
        }
    }
    if (g_self_syms.primary) {
        ::in_addr ip{};
        if (g_self_syms.primary(&ip) == 0 && subnet::isUsableHost(byteswap32(ip.s_addr))) {
            s.ip  = byteswap32(ip.s_addr);
            s.src = SelfSource::NifmPrimary;
            return s;
        }
    }
    const std::uint32_t ip = selfIpFromSockName();
    if (subnet::isUsableHost(ip)) {
        s.ip  = ip;
        s.src = SelfSource::SockName;
    }
    return s;
}

// Logged once per change, not once per retry.
void logSelfAddressIfChanged(const SelfAddress& s) {
    static std::uint32_t last_ip = 0xFFFFFFFFu;
    static std::uint32_t last_mask = 0xFFFFFFFFu;
    if (s.ip == last_ip && s.mask == last_mask) return;
    last_ip = s.ip;
    last_mask = s.mask;
    if (s.src == SelfSource::None) {
        MK8AP_LOG_WARN("[discover] console address unknown (no nifm/sockname answer); "
                       "own-subnet sweep disabled");
        return;
    }
    MK8AP_LOG_INFO("[discover] console address %u.%u.%u.%u mask %u.%u.%u.%u (via %s)",
                   (s.ip >> 24) & 0xFF, (s.ip >> 16) & 0xFF, (s.ip >> 8) & 0xFF, s.ip & 0xFF,
                   (s.mask >> 24) & 0xFF, (s.mask >> 16) & 0xFF, (s.mask >> 8) & 0xFF,
                   s.mask & 0xFF, selfSourceName(s.src));
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

// The optional developer seed (-DBRIDGE_HOST=...), host order; 0 if unset.
std::uint32_t bakedSeedIp() {
    if (BRIDGE_HOST_STRING[0] == '\0') return 0;
    ::in_addr ia{};
    if (nn::socket::InetAton(BRIDGE_HOST_STRING, &ia) == 0) {
        static bool warned = false;
        if (!warned) {
            warned = true;
            MK8AP_LOG_WARN("[discover] BRIDGE_HOST_STRING ('%s') is not an IPv4 literal; ignored",
                           BRIDGE_HOST_STRING);
        }
        return 0;
    }
    return byteswap32(ia.s_addr);
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

    // Learned up front (and logged on change) even when loopback answers,
    // so an emulator run exercises the same lookup a console depends on.
    const SelfAddress self = querySelfAddress();
    logSelfAddressIfChanged(self);
    report.self_ip = self.ip;
    report.subnet_mask = self.mask;

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

    // ---- Step 2: own subnet, plus the baked developer seed's /24 ----
    SweepRange ranges[2];
    int range_count = 0;
    const SweepRange own = subnet::sweepRange(self.ip, self.mask);
    if (!own.empty()) ranges[range_count++] = own;
    const std::uint32_t seed = bakedSeedIp();
    if (seed != 0 && !own.contains(seed)) {
        const SweepRange baked = subnet::sweepRange(seed, subnet::kMask24);
        if (!baked.empty()) ranges[range_count++] = baked;
    }
    if (range_count == 0) {
        publishReport(report);
        MK8AP_LOG_INFO("[discover] loopback silent and no subnet to sweep; "
                       "caller will retry with backoff");
        return false;
    }

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
