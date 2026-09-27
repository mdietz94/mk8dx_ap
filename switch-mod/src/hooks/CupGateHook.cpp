// Cup gating (AP location gate).
//
// Three layers, mirroring the "catch it upstream of the visible state
// change" rule from smo_archipelago M7:
//   1. gear::FlagAccessor::isOpen(EGrandPrixID)     -> save-flag query
//   2. gear::DLCManager::getCupStatus(EGrandPrixID)  -> entitlement query
//   3. ui::Control_CupButton::setCup(ECup, DLCState) -> what the button draws
//
// EGrandPrixID vs ECup: the public headers leave EGrandPrixID empty. We
// ASSUME EGrandPrixID == ECup - 1 (Mushroom = 0 ... Spiny = 23) and log
// every (id -> vanilla answer) pair the first time it is seen so the
// assumption can be checked against the boot log before trusting it.

#include "Hooks.hpp"

#include <atomic>
#include <cstdint>

#include "hk/hook/Trampoline.h"

#include "../ap/ApState.hpp"
#include "../game/Ids.hpp"
#include "../sym/PatternResolver.hpp"
#include "../util/Log.hpp"

namespace mk8ap::hooks {

namespace {

std::atomic<std::uint64_t> g_logged_gp{0};
std::atomic<std::uint64_t> g_logged_cup{0};

void logOnce(std::atomic<std::uint64_t>& seen, int id, const char* fmt, int a, int b) {
    if (id < 0 || id >= 64) return;
    const std::uint64_t bitv = 1ULL << id;
    if (seen.fetch_or(bitv) & bitv) return;
    MK8AP_LOG_INFO(fmt, a, b);
}

HkTrampoline<bool, const void*, int> g_isOpen = hk::hook::trampoline(
    [](const void* self, int gpId) -> bool {
        const bool vanilla = g_isOpen.orig(self, gpId);
        logOnce(g_logged_gp, gpId, "[cup] FlagAccessor::isOpen(gp=%d) vanilla=%d", gpId, vanilla ? 1 : 0);
        if (!ap::ApState::instance().isCupAllowed(gpId)) return false;
        return vanilla;
    });

HkTrampoline<int, void*, int> g_getCupStatus = hk::hook::trampoline(
    [](void* self, int gpId) -> int {
        const int vanilla = g_getCupStatus.orig(self, gpId);
        logOnce(g_logged_gp, gpId + 32, "[cup] DLCManager::getCupStatus(gp=%d) vanilla=%d", gpId, vanilla);
        if (!ap::ApState::instance().isCupAllowed(gpId)) {
            return static_cast<int>(game::DLCStatus::Locked);
        }
        return vanilla;
    });

HkTrampoline<std::uint64_t, void*, int, int> g_setCup = hk::hook::trampoline(
    [](void* self, int ecup, int dlcState) -> std::uint64_t {
        startNetworkingOnce("ui::Control_CupButton::setCup");
        logOnce(g_logged_cup, ecup, "[cup] Control_CupButton::setCup(ecup=%d, state=%d)", ecup, dlcState);
        const int cupIdx = ecup - 1;  // ECup::Mushroom == 1
        if (cupIdx >= 0 && cupIdx < game::kCupCount &&
            !ap::ApState::instance().isCupAllowed(cupIdx)) {
            dlcState = static_cast<int>(game::DLCState::Locked);
        }
        return g_setCup.orig(self, ecup, dlcState);
    });

template <typename H>
void install(H& hook, const char* name) {
    const auto off = sym::mainOffset(name);
    if (!off) { MK8AP_LOG_WARN("[cup] %s unresolved; skipped", name); return; }
    const auto rc = hook.installAtMainOffset(off);
    MK8AP_LOG_INFO("[cup] %s @+0x%llx: %s", name, static_cast<unsigned long long>(off),
                   rc.succeeded() ? "installed" : "FAILED");
}

}  // namespace

void installCupGateHooks() {
    install(g_isOpen, "gear::FlagAccessor::isOpen");
    install(g_getCupStatus, "gear::DLCManager::getCupStatus");
    install(g_setCup, "ui::Control_CupButton::setCup");
}

}  // namespace mk8ap::hooks
