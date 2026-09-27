// gear::SaveDataFile::load(int): the game is fully initialised and a user
// profile has been chosen. One-shot: start the AP worker and dump the
// driver / vehicle id tables.

#include "Hooks.hpp"

#include <cstdint>

#include "hk/hook/Trampoline.h"

#include "../ap/ApClient.hpp"
#include "../ap/ApConfig.hpp"
#include "../ap/ApState.hpp"
#include "../sym/PatternResolver.hpp"
#include "../util/Log.hpp"

namespace mk8ap::hooks {

namespace {

HkTrampoline<std::uint64_t, void*, int> g_load = hk::hook::trampoline(
    [](void* self, int slot) -> std::uint64_t {
        MK8AP_LOG_INFO("[save] SaveDataFile::load(this=%p, %d)", self, slot);
        const std::uint64_t ret = g_load.orig(self, slot);
        startNetworkingOnce("gear::SaveDataFile::load");
        return ret;
    });

}  // namespace

void startNetworkingOnce(const char* trigger) {
    auto& st = ap::ApState::instance();
    if (st.networking_started.load(std::memory_order_relaxed)) return;
    bool expected = false;
    if (!st.networking_started.compare_exchange_strong(expected, true)) return;
    MK8AP_LOG_INFO("[net] starting networking (trigger: %s)", trigger ? trigger : "?");
    const auto cfg = ap::loadApConfig();
    ap::ApClient::instance().start(ap::BridgeTarget{
        .host = cfg.bridge_host,
        .port = cfg.bridge_port,
        .retry_ms = cfg.retry_ms,
        .recv_timeout_ms = cfg.recv_timeout_ms,
    });
    dumpIdTablesOnce();
}

void installSaveLoadHook() {
    const auto off = sym::mainOffset("gear::SaveDataFile::load");
    if (!off) {
        MK8AP_LOG_WARN("[save] SaveDataFile::load unresolved; networking will start from "
                       "the first menu / race / PlayReport hook instead");
        return;
    }
    const auto rc = g_load.installAtMainOffset(off);
    MK8AP_LOG_INFO("[save] hook @+0x%llx: %s", static_cast<unsigned long long>(off),
                   rc.succeeded() ? "installed" : "FAILED");
}

}  // namespace mk8ap::hooks
