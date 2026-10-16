// Cup gating (AP location gate).
//
// Three layers, mirroring the "catch it upstream of the visible state
// change" rule from smo_archipelago M7:
//   1. gear::FlagAccessor::isOpen(EGrandPrixID)     -> save-flag query
//   2. gear::DLCManager::getCupStatus(EGrandPrixID)  -> entitlement query
//   3. ui::Control_CupButton::setCup(ECup, DLCState) -> what the button draws
//
// SIGNATURE SAFETY (4.0.0 first-boot lesson, 2026-10-16): the pattern-located
// functions were declared with `int` arguments taken from the upstream
// mangled names. On 4.0.0 the function the setCup pattern lands on receives
// POINTERS in x1/x2 (its second instruction is `ldr w8, [x20]` with
// x20 = x1); declaring them `int` truncated the pointers to 32 bits on the
// way into orig() and faulted on the "Prepare Thread" during boot.
//
// Rule now: every pattern-resolved hook takes its integer registers as
// u64 and forwards them UNCHANGED. The value is interpreted as an enum only
// when it is plausibly one (small non-negative), and the raw registers are
// logged once per distinct low bits so the boot log can confirm the real
// signature before any gating is trusted.
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

using u64 = std::uint64_t;

std::atomic<std::uint64_t> g_logged_gp{0};
std::atomic<std::uint64_t> g_logged_cup{0};
std::atomic<std::uint64_t> g_logged_status{0};

// Returns true the first time `slot` (0..63) is seen.
bool firstTime(std::atomic<std::uint64_t>& seen, unsigned slot) {
    const std::uint64_t bitv = 1ULL << (slot & 63);
    return (seen.fetch_or(bitv) & bitv) == 0;
}

// A register value that is plausibly a small enum rather than a pointer.
bool looksLikeEnum(u64 v) { return v < 0x100; }

// --- 1. gear::FlagAccessor::isOpen(this, EGrandPrixID) const -> bool -----
// Pattern-resolved. x1..x3 forwarded as-is; only gates when x1 is a small int.
HkTrampoline<u64, const void*, u64, u64, u64> g_isOpen = hk::hook::trampoline(
    [](const void* self, u64 x1, u64 x2, u64 x3) -> u64 {
        const u64 vanilla = g_isOpen.orig(self, x1, x2, x3);
        if (firstTime(g_logged_gp, static_cast<unsigned>(x1 & 0x3f))) {
            MK8AP_LOG_INFO("[cup] FlagAccessor::isOpen x1=0x%llx x2=0x%llx x3=0x%llx -> 0x%llx",
                           static_cast<unsigned long long>(x1), static_cast<unsigned long long>(x2),
                           static_cast<unsigned long long>(x3), static_cast<unsigned long long>(vanilla));
        }
        if (looksLikeEnum(x1)) {
            const int gpId = static_cast<int>(x1);
            if (!ap::ApState::instance().isCupAllowed(gpId)) return 0;  // false
        }
        return vanilla;
    });

// --- 2. gear::DLCManager::getCupStatus(this, EGrandPrixID) -> DLCStatus --
HkTrampoline<u64, void*, u64, u64, u64> g_getCupStatus = hk::hook::trampoline(
    [](void* self, u64 x1, u64 x2, u64 x3) -> u64 {
        const u64 vanilla = g_getCupStatus.orig(self, x1, x2, x3);
        if (firstTime(g_logged_status, static_cast<unsigned>(x1 & 0x3f))) {
            MK8AP_LOG_INFO("[cup] DLCManager::getCupStatus x1=0x%llx x2=0x%llx -> 0x%llx",
                           static_cast<unsigned long long>(x1), static_cast<unsigned long long>(x2),
                           static_cast<unsigned long long>(vanilla));
        }
        if (looksLikeEnum(x1)) {
            const int gpId = static_cast<int>(x1);
            if (!ap::ApState::instance().isCupAllowed(gpId)) {
                return static_cast<u64>(static_cast<int>(game::DLCStatus::Locked));
            }
        }
        return vanilla;
    });

// --- 3. ui::Control_CupButton::setCup -------------------------------------
// OBSERVE ONLY until the 4.0.0 signature is confirmed from the boot log.
// On 4.0.0 x1 and x2 arrive as pointers at least on the boot-time call path,
// so the (ECup, DLCState) reading from the upstream mangled name is wrong or
// incomplete for this address. All of x1..x5 are forwarded verbatim.
HkTrampoline<u64, void*, u64, u64, u64, u64, u64> g_setCup = hk::hook::trampoline(
    [](void* self, u64 x1, u64 x2, u64 x3, u64 x4, u64 x5) -> u64 {
        startNetworkingOnce("ui::Control_CupButton::setCup");
        if (firstTime(g_logged_cup, static_cast<unsigned>(x1 & 0x3f))) {
            MK8AP_LOG_INFO("[cup] Control_CupButton::setCup this=%p x1=0x%llx x2=0x%llx x3=0x%llx x4=0x%llx",
                           self, static_cast<unsigned long long>(x1), static_cast<unsigned long long>(x2),
                           static_cast<unsigned long long>(x3), static_cast<unsigned long long>(x4));
        }
        // Gating deliberately disabled until the signature is known. Once
        // x1/x2 are confirmed to be (ECup, DLCState) on the menu path,
        // re-enable: if (!isCupAllowed(ecup-1)) x2 = DLCState::Locked.
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
