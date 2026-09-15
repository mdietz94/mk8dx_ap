// TCP client to the PC bridge (MK8DX Client).
//
// One persistent connection on a dedicated worker thread. Game threads only
// touch ApState's rings/atomics; this thread does all blocking I/O.
//
// Lifecycle:
//   1. hkMain installs the nn::socket::Initialize trampoline (SocketInitHook)
//      so we know whether the game has brought sockets up.
//   2. SaveLoadHook (first gear::SaveDataFile::load) calls start() on the
//      game thread: nifm bring-up, socket init if the game has not done it,
//      then spawns the worker via nn::os::CreateThread (nn::socket needs an
//      nn::os-created thread for its TLS-backed mutexes).
//   3. threadMain(): discover -> connect -> HELLO -> poll/recv/pump loop,
//      exponential backoff (1,2,5,10,30s) on failure.

#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <string>

namespace mk8ap::ap {

inline constexpr std::size_t kInboundLineCap = 8 * 1024;

struct BridgeTarget {
    std::string host;
    std::uint16_t port = 17778;
    std::uint32_t retry_ms = 3000;
    std::uint32_t recv_timeout_ms = 200;
};

class ApClient {
public:
    static ApClient& instance();

    // Game-thread entry. Idempotent.
    void start(const BridgeTarget& target);

    void threadMain();  // worker entry (public for the trampoline)

private:
    ApClient() = default;

    bool connectOnce();
    void disconnect();
    void sendHello();
    void pumpOutbound();
    bool recvIntoBuf();
    bool popLine(char* out, std::size_t& out_len);
    void handleLine(char* line, std::size_t len);
    bool sendLine(const char* data, std::size_t len);

    BridgeTarget target_{};
    std::atomic<bool> started_{false};
    std::int32_t socket_fd_ = -1;
    char read_buf_[kInboundLineCap]{};
    std::size_t read_buf_len_ = 0;
};

}  // namespace mk8ap::ap
