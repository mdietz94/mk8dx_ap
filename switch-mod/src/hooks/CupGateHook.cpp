// Cup gating (AP location gate).
//
// Three layers, mirroring the "catch it upstream of the visible state
// change" rule from smo_archipelago M7:
//   1. gear::FlagAccessor::isOpen(const EGrandPrixID&)      -> save-flag query
//   2. gear::DLCManager::getCupStatus(const EGrandPrixID&)  -> entitlement query
//   3. ui::Control_CupButton::setCup(const ECup&, const DLCState&) -> button state
//
// CALLING CONVENTION (confirmed on 4.0.0, 2026-10-16): the mush::E*ID /
// ui::ECup / ui::DLCState "enums" are class wrappers (they carry static
// text_() members), so they travel BY CONST REFERENCE: x1/x2 are pointers to
// a 32-bit value on the caller's stack, not the value itself. The first
// build declared them `int`, truncated the pointers on the way into orig()
// and faulted on the Prepare Thread at boot. Every hook here now forwards
// the registers as u64 and dereferences them only when they look like
// pointers; to override an argument it points orig() at a local copy.
//
// EGrandPrixID vs ECup: CONFIRMED on 4.0.0 (2026-10-16 boot log) that the
// two are the same 0-based index (Mushroom = 0 ... Spiny = 23): the title
// screen's cup grid calls isOpen(gp=N) immediately followed by setCup(ecup=N).

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

using u64 = std::uint64_t;

std::atomic<std::uint64_t> g_logged_gp{0};
std::atomic<std::uint64_t> g_logged_cup{0};
std::atomic<std::uint64_t> g_logged_status{0};

// Returns true the first time `slot` (0..63) is seen.
bool firstTime(std::atomic<std::uint64_t>& seen, int slot) {
    if (slot < 0) slot = 63;
    const std::uint64_t bitv = 1ULL << (slot & 63);
    return (seen.fetch_or(bitv) & bitv) == 0;
}

// Read the 32-bit enum value a by-reference argument points at. Returns
// -1 when the register does not look like a pointer (so callers never
// dereference a small immediate by mistake).
int derefEnum(u64 reg) {
    if (reg < 0x10000) return -1;
    return *reinterpret_cast<const std::int32_t*>(reg);
}

// --- 1. gear::FlagAccessor::isOpen(this, const EGrandPrixID&) const -> bool
HkTrampoline<u64, const void*, u64, u64, u64> g_isOpen = hk::hook::trampoline(
    [](const void* self, u64 x1, u64 x2, u64 x3) -> u64 {
        const u64 vanilla = g_isOpen.orig(self, x1, x2, x3);
        const int gpId = derefEnum(x1);
        if (firstTime(g_logged_gp, gpId)) {
            MK8AP_LOG_INFO("[cup] FlagAccessor::isOpen(gp=%d) [x1=0x%llx] vanilla=%d",
                           gpId, static_cast<unsigned long long>(x1), static_cast<int>(vanilla & 1));
        }
        // EXPERIMENT (2026-10-16): returning false here soft-locked the game
        // when a locked cup was selected (Deluxe never has closed cups, so the
        // UI has no path for it). Pass through and log; the button-state
        // layer (setCup -> Locked) carries the gate alone for this test.
        static int denials_logged = 0;
        if (gpId >= 0 && gpId < game::kCupCount &&
            !ap::ApState::instance().isCupAllowed(gpId) && denials_logged < 40) {
            ++denials_logged;
            MK8AP_LOG_INFO("[cup] isOpen(gp=%d) would deny (passing vanilla=%d)", gpId,
                           static_cast<int>(vanilla & 1));
        }
        return vanilla;
    });

// --- 2. gear::DLCManager::getCupStatus(this, const EGrandPrixID&) -> DLCStatus
HkTrampoline<u64, void*, u64, u64, u64> g_getCupStatus = hk::hook::trampoline(
    [](void* self, u64 x1, u64 x2, u64 x3) -> u64 {
        const u64 vanilla = g_getCupStatus.orig(self, x1, x2, x3);
        const int gpId = derefEnum(x1);
        if (firstTime(g_logged_status, gpId)) {
            MK8AP_LOG_INFO("[cup] DLCManager::getCupStatus(gp=%d) [x1=0x%llx] vanilla=%d",
                           gpId, static_cast<unsigned long long>(x1), static_cast<int>(vanilla));
        }
        if (gpId >= 0 && gpId < game::kCupCount &&
            !ap::ApState::instance().isCupAllowed(gpId)) {
            return static_cast<u64>(static_cast<int>(game::DLCStatus::Locked));
        }
        return vanilla;
    });

// --- 3. ui::Control_CupButton::setCup(this, const ECup&, const DLCState&) --
// To lock a cup we hand orig() a pointer to a local DLCState::Locked instead
// of the caller's value; the caller's object is never written.
HkTrampoline<u64, void*, u64, u64, u64, u64, u64> g_setCup = hk::hook::trampoline(
    [](void* self, u64 x1, u64 x2, u64 x3, u64 x4, u64 x5) -> u64 {
        startNetworkingOnce("ui::Control_CupButton::setCup");
        const int ecup = derefEnum(x1);
        const int state = derefEnum(x2);
        if (firstTime(g_logged_cup, ecup)) {
            MK8AP_LOG_INFO("[cup] Control_CupButton::setCup(ecup=%d, state=%d) [x1=0x%llx x2=0x%llx x3=0x%llx]",
                           ecup, state, static_cast<unsigned long long>(x1),
                           static_cast<unsigned long long>(x2), static_cast<unsigned long long>(x3));
        }
        // 4.0.0 boot log: setCup(ecup=N) is always paired with isOpen(gp=N),
        // so ECup is 0-based (Mushroom == 0) and equals EGrandPrixID. The
        // upstream header's "Mushroom == 1" reading was wrong for this build.
        const int cupIdx = ecup;
        if (state >= 0 && cupIdx >= 0 && cupIdx < game::kCupCount &&
            !ap::ApState::instance().isCupAllowed(cupIdx)) {
            const std::int32_t locked = static_cast<std::int32_t>(game::DLCState::Locked);
            static int lock_logged = 0;
            if (lock_logged < 40) {
                ++lock_logged;
                MK8AP_LOG_INFO("[cup] setCup(ecup=%d) state %d -> Locked(%d)", ecup, state, locked);
            }
            return g_setCup.orig(self, x1, reinterpret_cast<u64>(&locked), x3, x4, x5);
        }
        return g_setCup.orig(self, x1, x2, x3, x4, x5);
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
