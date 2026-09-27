// See ApClient.hpp. Port of smo_archipelago / smbw_ap ApClient.cpp with the
// game-specific message set replaced by the MK8DX one.

#include "ApClient.hpp"

#include <cstdint>
#include <cstring>

#include "hk/svc/api.h"
#include "hk/types.h"

#include "ApDiscovery.hpp"
#include "ApProtocol.hpp"
#include "ApState.hpp"
#include "../sym/PatternResolver.hpp"
#include "../util/Log.hpp"

// nn::os thread API. The worker MUST be an nn::os thread: nn::socket's
// bsd:u session lock is an nn::os mutex that reads a per-thread TLS slot
// only nn::os::CreateThread sets up (a raw SVC thread faults on the first
// socket call; see smbw_ap history).
namespace nn::os {
    struct ThreadType;
    void CreateThread(ThreadType* type, void (*entry)(void*), void* arg,
                      void* stack, size_t stack_size, int priority, int ideal_core);
    void StartThread(ThreadType* type);
}

namespace nn::nifm {
    std::uint32_t Initialize();
    void SubmitNetworkRequestAndWait();
    bool IsNetworkAvailable();
}

// Nintendo bsd:u sockaddr layout (NOT POSIX). Names must be `sockaddr` /
// `in_addr` / `pollfd` at file scope: the mangled symbols nnrtld resolves
// (e.g. _ZN2nn6socket7ConnectEiPK8sockaddrj) encode these type names.
struct in_addr { u32 s_addr; };
struct sockaddr {
    u8 sa_len;
    u8 sa_family;
    u16 sa_port;  // network byte order
    in_addr sa_addr;
    u8 sa_zero[8];
};
struct pollfd { s32 fd; short events; short revents; };

namespace nn::socket {
    s32 Socket(s32 domain, s32 type, s32 protocol);
    u32 Connect(s32 socket, const ::sockaddr* addr, u32 addrLen);
    s32 Send(s32 socket, const void* data, unsigned long len, s32 flags);
    s32 Recv(s32 socket, void* out, unsigned long len, s32 flags);
    s32 SetSockOpt(s32 socket, s32 level, s32 option, const void* val, u32 len);
    u32 Close(s32 socket);
    s32 Poll(::pollfd* fds, unsigned long n, s32 timeout_ms);
    u16 InetHtons(u16 val);
    s32 InetAton(const char* str, ::in_addr* out);
    s32 GetLastErrno();
}

// SocketInitHook.cpp
namespace mk8ap::hooks {
bool socketInitialized();
bool ensureSocketInitialized();
}

namespace mk8ap::ap {

namespace {

constexpr s32 kAfInet      = 2;
constexpr s32 kSockStream  = 1;
constexpr s32 kIpprotoTcp  = 6;
constexpr s32 kPollIn      = 0x0001;
constexpr s32 kPollErr     = 0x0008;
constexpr s32 kPollHup     = 0x0010;
constexpr s32 kSolSocket   = 0xffff;
constexpr s32 kSoKeepAlive = 0x0008;

constexpr std::size_t kWorkerStackSize = 0x10000;  // 64 KiB
alignas(0x1000) u8 g_worker_stack[kWorkerStackSize];
alignas(8) u8 g_worker_thread_storage[0x1C0];  // sizeof(nn::os::ThreadType)

constexpr std::uint32_t kBackoffMs[] = {1000, 2000, 5000, 10000, 30000};
constexpr std::size_t kBackoffCount = sizeof(kBackoffMs) / sizeof(kBackoffMs[0]);

void sleepMs(std::int64_t ms) { hk::svc::SleepThread(ms * 1'000'000ll); }

bool sockAddrFromIpv4(const char* host, std::uint16_t port, ::sockaddr& out) {
    ::in_addr ia{};
    if (nn::socket::InetAton(host, &ia) == 0) return false;
    out = ::sockaddr{};
    out.sa_len    = sizeof(out);
    out.sa_family = static_cast<u8>(kAfInet);
    out.sa_port   = nn::socket::InetHtons(port);
    out.sa_addr   = ia;
    return true;
}

int pollReadable(s32 fd, std::uint32_t timeout_ms) {
    ::pollfd pfd{ .fd = fd, .events = static_cast<short>(kPollIn), .revents = 0 };
    const s32 n = nn::socket::Poll(&pfd, 1, static_cast<s32>(timeout_ms));
    if (n < 0) return -1;
    if (n == 0) return 0;
    if (pfd.revents & (kPollErr | kPollHup)) return -1;
    return (pfd.revents & kPollIn) ? 1 : 0;
}

void workerEntry(void* arg) {
    static_cast<ApClient*>(arg)->threadMain();
    while (true) hk::svc::SleepThread(INT64_MAX);
}

}  // namespace

ApClient& ApClient::instance() {
    static ApClient s;
    return s;
}

void ApClient::start(const BridgeTarget& target) {
    bool expected = false;
    if (!started_.compare_exchange_strong(expected, true)) {
        MK8AP_LOG_INFO("[net] start: already running");
        return;
    }
    target_ = target;

    // nifm: the game has almost certainly initialised it already (MK8DX is
    // an online title), in which case Initialize returns an error we ignore.
    const std::uint32_t nifm_rc = nn::nifm::Initialize();
    MK8AP_LOG_INFO("[net] nn::nifm::Initialize rc=0x%x", nifm_rc);
    // Only submit our own request when the game has not already brought
    // the network up: start() may run inside a menu or race hook, and
    // SubmitNetworkRequestAndWait blocks the calling (game) thread.
    if (!nn::nifm::IsNetworkAvailable()) nn::nifm::SubmitNetworkRequestAndWait();
    MK8AP_LOG_INFO("[net] network available: %s",
                   nn::nifm::IsNetworkAvailable() ? "YES" : "NO");

    if (!mk8ap::hooks::ensureSocketInitialized()) {
        MK8AP_LOG_ERROR("[net] nn::socket not initialised and our own Initialize "
                        "failed; worker not started");
        started_.store(false);
        return;
    }

    auto* tt = reinterpret_cast<nn::os::ThreadType*>(g_worker_thread_storage);
    nn::os::CreateThread(tt, &workerEntry, this, g_worker_stack, kWorkerStackSize,
                         /*priority=*/28, /*ideal_core=*/-2);
    nn::os::StartThread(tt);
    MK8AP_LOG_INFO("[net] worker thread started (seed host %s:%u)",
                   target_.host.c_str(), target_.port);
}

void ApClient::threadMain() {
    MK8AP_LOG_INFO("[worker] connect loop entered");
    std::size_t backoff = 0;
    auto& st = ApState::instance();

    while (true) {
        st.conn.store(ConnState::Connecting, std::memory_order_relaxed);
        BridgeTarget discovered{};
        bool ok = resolveBridge(discovered);
        if (ok) {
            target_.host = discovered.host;
            target_.port = discovered.port;
            ok = connectOnce();
        }
        if (!ok) {
            const std::uint32_t wait = kBackoffMs[backoff];
            MK8AP_LOG_INFO("[conn] no bridge; retry in %u ms", wait);
            sleepMs(wait);
            if (backoff + 1 < kBackoffCount) ++backoff;
            continue;
        }
        backoff = 0;
        st.conn.store(ConnState::Hello, std::memory_order_relaxed);
        sendHello();

        while (true) {
            pumpOutbound();
            const int p = pollReadable(socket_fd_, target_.recv_timeout_ms);
            if (p < 0) { MK8AP_LOG_WARN("[conn] poll error; reconnecting"); break; }
            if (p > 0) {
                if (!recvIntoBuf()) { MK8AP_LOG_INFO("[conn] peer closed"); break; }
                char line[kInboundLineCap];
                std::size_t line_len = 0;
                while (popLine(line, line_len)) handleLine(line, line_len);
            }
        }
        disconnect();
        sleepMs(kBackoffMs[0]);
    }
}

bool ApClient::connectOnce() {
    socket_fd_ = nn::socket::Socket(kAfInet, kSockStream, kIpprotoTcp);
    if (socket_fd_ < 0) {
        MK8AP_LOG_WARN("[conn] Socket() failed errno=%d", nn::socket::GetLastErrno());
        socket_fd_ = -1;
        return false;
    }
    ::sockaddr addr{};
    if (!sockAddrFromIpv4(target_.host.c_str(), target_.port, addr)) {
        MK8AP_LOG_WARN("[conn] bad host literal %s", target_.host.c_str());
        disconnect();
        return false;
    }
    const u32 rc = nn::socket::Connect(socket_fd_, &addr, sizeof(addr));
    if (rc != 0) {
        MK8AP_LOG_WARN("[conn] Connect %s:%u failed errno=%d", target_.host.c_str(),
                       target_.port, nn::socket::GetLastErrno());
        disconnect();
        return false;
    }
    const s32 one = 1;
    (void)nn::socket::SetSockOpt(socket_fd_, kSolSocket, kSoKeepAlive, &one, sizeof(one));
    MK8AP_LOG_INFO("[conn] CONNECTED %s:%u fd=%d", target_.host.c_str(), target_.port, socket_fd_);
    return true;
}

void ApClient::disconnect() {
    if (socket_fd_ >= 0) {
        (void)nn::socket::Close(socket_fd_);
        socket_fd_ = -1;
    }
    read_buf_len_ = 0;
    auto& st = ApState::instance();
    st.conn.store(ConnState::Disconnected, std::memory_order_relaxed);
    st.bridge_ready.store(false, std::memory_order_relaxed);
}

bool ApClient::sendLine(const char* data, std::size_t len) {
    std::size_t off = 0;
    while (off < len) {
        const s32 n = nn::socket::Send(socket_fd_, data + off, len - off, 0);
        if (n <= 0) {
            MK8AP_LOG_WARN("[send] Send returned %d errno=%d", n, nn::socket::GetLastErrno());
            return false;
        }
        off += static_cast<std::size_t>(n);
    }
    return true;
}

void ApClient::sendHello() {
    Hello h{};
    copyFixed(h.mod_ver, MK8AP_MOD_VERSION_STRING);
    copyFixed(h.game_ver, MK8DX_VERSION_STRING);
    copyFixed(h.build_id, sym::buildIdHex());
    h.resolved = sym::resolvedCount();
    h.total = sym::totalCount();
    sym::unresolvedNames(h.unresolved, sizeof(h.unresolved));
    util::json::LineBuffer line;
    encodeHello(line, h);
    if (sendLine(line.data(), line.size())) {
        MK8AP_LOG_INFO("[conn] HELLO sent (%zu bytes)", line.size());
    }
}

void ApClient::pumpOutbound() {
    auto& st = ApState::instance();
    util::json::LineBuffer line;

    RaceResult r;
    while (st.outbound_results.peek(r)) {
        encodeRaceResult(line, r);
        if (!sendLine(line.data(), line.size())) return;
        st.outbound_results.popDiscard();
        MK8AP_LOG_INFO("[pump] race_result seq=%d course=%d rank=%d sent", r.seq, r.course_id, r.rank);
    }
    while (const PlayReport* pr = st.outbound_reports.peekRef()) {
        encodePlayReport(line, *pr);
        if (line.truncated()) {
            MK8AP_LOG_WARN("[pump] play_report line truncated (size=%zu); dropping", pr->size);
            st.outbound_reports.popDiscard();
            continue;
        }
        if (!sendLine(line.data(), line.size())) return;
        st.outbound_reports.popDiscard();
        MK8AP_LOG_INFO("[pump] play_report room=%s (%zu bytes) sent", pr->room, pr->size);
    }
    while (const IdTable* t = st.outbound_tables.peekRef()) {
        encodeIdTable(line, *t);
        if (!sendLine(line.data(), line.size())) return;
        st.outbound_tables.popDiscard();
        MK8AP_LOG_INFO("[pump] id_table kind=%s (%zu entries) sent", t->kind, t->count);
    }
    if (const std::uint32_t drops = st.log_drops.exchange(0, std::memory_order_relaxed); drops > 0) {
        Log marker;
        copyFixed(marker.level, "warn");
        std::snprintf(marker.msg, sizeof(marker.msg), "[log_forward] %u line(s) dropped", drops);
        encodeLog(line, marker);
        if (!sendLine(line.data(), line.size())) { st.log_drops.fetch_add(drops); return; }
    }
    Log lg;
    while (st.outbound_logs.peek(lg)) {
        encodeLog(line, lg);
        if (!sendLine(line.data(), line.size())) return;
        st.outbound_logs.popDiscard();
    }
}

bool ApClient::recvIntoBuf() {
    const std::size_t avail = kInboundLineCap - read_buf_len_;
    if (avail == 0) {
        char drain[256];
        (void)nn::socket::Recv(socket_fd_, drain, sizeof(drain), 0);
        read_buf_len_ = 0;  // overlong line: resync
        return true;
    }
    const std::size_t cap = avail < 1024 ? avail : 1024;
    const s32 n = nn::socket::Recv(socket_fd_, read_buf_ + read_buf_len_, cap, 0);
    if (n <= 0) return false;
    read_buf_len_ += static_cast<std::size_t>(n);
    return true;
}

bool ApClient::popLine(char* out, std::size_t& out_len) {
    std::size_t nl = read_buf_len_;
    for (std::size_t i = 0; i < read_buf_len_; ++i) {
        if (read_buf_[i] == '\n') { nl = i; break; }
    }
    if (nl == read_buf_len_) {
        if (read_buf_len_ >= kInboundLineCap) read_buf_len_ = 0;
        return false;
    }
    std::memcpy(out, read_buf_, nl);
    out_len = nl;
    const std::size_t consumed = nl + 1;
    const std::size_t remaining = read_buf_len_ - consumed;
    std::memmove(read_buf_, read_buf_ + consumed, remaining);
    read_buf_len_ = remaining;
    return true;
}

void ApClient::handleLine(char* line, std::size_t len) {
    static DecodedMsg m;  // large; keep in BSS
    if (!decode(line, len, m)) {
        MK8AP_LOG_WARN("[recv] malformed line: %.*s", static_cast<int>(len < 120 ? len : 120), line);
        return;
    }
    auto& st = ApState::instance();
    auto eq = [&](const char* s) { return std::strcmp(m.t, s) == 0; };
    if (eq("hello_ack")) {
        if (!m.hello_ack.ok) {
            MK8AP_LOG_ERROR("[recv] hello_ack REJECTED: %s (client %s / mod %s)",
                            m.hello_ack.err, m.hello_ack.client_ver, MK8AP_MOD_VERSION_STRING);
            return;
        }
        copyFixed(st.local_slot, m.hello_ack.slot);
        copyFixed(st.seed, m.hello_ack.seed);
        st.conn.store(ConnState::Ready, std::memory_order_release);
        st.bridge_ready.store(true, std::memory_order_release);
        MK8AP_LOG_INFO("[recv] hello_ack ok seed=%s slot=%s client=%s",
                       m.hello_ack.seed, m.hello_ack.slot, m.hello_ack.client_ver);
    } else if (eq("state")) {
        st.applyState(m.state);
    } else if (eq("print")) {
        MK8AP_LOG_INFO("[bridge] %s", m.print.text);
    } else if (eq("pong")) {
        // liveness only
    } else if (eq("err")) {
        MK8AP_LOG_WARN("[recv] bridge err code=%s ctx=%s", m.err.code, m.err.ctx);
    } else {
        MK8AP_LOG_WARN("[recv] unknown message t=%s", m.t);
    }
}

}  // namespace mk8ap::ap
