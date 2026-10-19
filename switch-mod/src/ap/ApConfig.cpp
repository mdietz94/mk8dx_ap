// AP bridge config: compile-time defaults baked in via CMake, plus the
// ap_config.json sweep seed cached by RomConfigHook.

#include "ApConfig.hpp"

#include <atomic>
#include <cstring>

#include "../util/Log.hpp"

namespace mk8ap::ap {

namespace {
char g_cfg_host[64];
std::atomic<bool> g_cfg_host_valid{false};
}  // namespace

ApConfig loadApConfig() {
    ApConfig cfg;
    char seed[sizeof(g_cfg_host)];
    MK8AP_LOG_INFO("ApConfig: sweep seed=%s bridge_port=%u retry=%ums recv_to=%ums",
                   configuredBridgeHost(seed, sizeof(seed)) ? seed : "(none: loopback only)",
                   cfg.bridge_port, cfg.retry_ms, cfg.recv_timeout_ms);
    return cfg;
}

bool setConfiguredBridgeHost(std::string_view host) {
    if (g_cfg_host_valid.load(std::memory_order_acquire)) return false;
    if (host.empty() || host.size() >= sizeof(g_cfg_host)) return false;
    std::memcpy(g_cfg_host, host.data(), host.size());
    g_cfg_host[host.size()] = '\0';
    g_cfg_host_valid.store(true, std::memory_order_release);
    return true;
}

bool configuredBridgeHost(char* out, std::size_t cap) {
    if (!g_cfg_host_valid.load(std::memory_order_acquire)) return false;
    if (out == nullptr) return true;
    const std::size_t n = std::strlen(g_cfg_host);
    if (n >= cap) return false;
    std::memcpy(out, g_cfg_host, n + 1);
    return true;
}

}  // namespace mk8ap::ap
