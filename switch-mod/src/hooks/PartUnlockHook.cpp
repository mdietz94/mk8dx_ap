// Menu-side unlocks for AP-received drivers / bodies / tires / gliders.
//
// On a fresh save most kart parts are coin unlocks (and Gold Mario a 200cc
// unlock), so an AP-received part was not selectable until the save had
// earned it. The select screens ask the save through
//   bool gear::FlagAccessor::isOpen(const mush::EDriverID&) const   +0x865F14
//   bool gear::FlagAccessor::isOpen(const mush::EBodyID&)   const   +0x865F60
//   bool gear::FlagAccessor::isOpen(const mush::ETireID&)   const   +0x865FAC
//   bool gear::FlagAccessor::isOpen(const mush::EWingID&)   const   +0x865FF8
// (4.0.1 disassembly, 2026-11-04). They sit right after the cup overload
// (isOpen(EGrandPrixID) +0x865E7C) and are byte-identical apart from the
// table they read: save+0x188 -> byte[id] at +0x1F48 (drivers, 64 slots),
// +0x1F88 (bodies), +0x1FC8 (tires), +0x2008 (gliders); a part is open when
// its byte is 3 (1 / 2 are the "just unlocked" / "new" stages the unlock
// presentation walks through). Which overload is which was read off the
// callers: the parts-list builder at +0x474F98 calls the body overload once
// per id (41 calls), the tire one 22 times, the glider one 15 times;
// +0x866DFC scans exactly 42 bytes of the +0x1F88 table; and the loadout
// sanitiser at +0x3FD508 checks driver/body/tire/glider fields in that order,
// zeroing any that are not open.
//
// The hook only ever OPENS: it returns true when the save says closed but the
// AP state allows the id. It never returns false for a part the save has open
// (closing FlagAccessor::isOpen for cups soft-locked the cup grid; locked
// picks are swapped at race start by KartGuardHook instead). Without an AP
// slot (restricting() false) it is a pass-through. Only base-game ids are
// opened: drivers 44..52 are Booster Course Pass characters and stay with the
// game's own entitlement logic, so an unowned DLC driver is never exposed.
//
// ABI: the mush::E*ID wrappers travel by const reference (x1 = pointer to
// the 32-bit id), as for every other enum hook here. Function-entry
// trampolines only; the first four instructions are plain stack setup.

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

int derefEnum(u64 reg) {
    if (reg < 0x10000) return -1;
    return *reinterpret_cast<const std::int32_t*>(reg);
}

enum class Part : int { Driver, Body, Tire, Wing };

struct PartInfo {
    const char* label;
    const char* symbol;
    int baseCount;  // ids [0, baseCount) may be opened
};

constexpr PartInfo kParts[] = {
    {"driver", "gear::FlagAccessor::isOpen(EDriverID)", game::kBaseDriverCount},
    {"body", "gear::FlagAccessor::isOpen(EBodyID)", game::kBodyCount},
    {"tire", "gear::FlagAccessor::isOpen(ETireID)", game::kTireCount},
    {"glider", "gear::FlagAccessor::isOpen(EWingID)", game::kWingCount},
};

std::atomic<bool> g_firstCall[4]{};         // per-category first-call log
std::atomic<std::uint64_t> g_opened[4]{};   // ids this hook opened (log once each)

bool allowed(Part p, int id) {
    const auto& st = ap::ApState::instance();
    switch (p) {
        case Part::Driver: return st.isDriverAllowed(id);
        case Part::Body: return st.isBodyAllowed(id);
        case Part::Tire: return st.isTireAllowed(id);
        case Part::Wing: return st.isWingAllowed(id);
    }
    return false;
}

// Shared body: `vanilla` is the original's result for the id behind x1.
u64 decide(Part p, u64 x1, u64 vanilla) {
    const int k = static_cast<int>(p);
    const int id = derefEnum(x1);
    if (!g_firstCall[k].exchange(true)) {
        MK8AP_LOG_INFO("[unlock] %s isOpen first call: id=%d [x1=0x%llx] vanilla=%d",
                       kParts[k].label, id, static_cast<unsigned long long>(x1),
                       static_cast<int>(vanilla & 1));
    }
    if (vanilla & 1) return vanilla;                       // never close
    if (!ap::ApState::instance().restricting()) return vanilla;
    if (id < 0 || id >= kParts[k].baseCount) return vanilla;
    if (!allowed(p, id)) return vanilla;
    const std::uint64_t bitv = 1ULL << id;
    if ((g_opened[k].fetch_or(bitv) & bitv) == 0) {
        MK8AP_LOG_INFO("[unlock] %s %d closed in save, open via AP", kParts[k].label, id);
    }
    return 1;
}

HkTrampoline<u64, const void*, u64, u64, u64> g_isOpenDriver = hk::hook::trampoline(
    [](const void* self, u64 x1, u64 x2, u64 x3) -> u64 {
        return decide(Part::Driver, x1, g_isOpenDriver.orig(self, x1, x2, x3));
    });
HkTrampoline<u64, const void*, u64, u64, u64> g_isOpenBody = hk::hook::trampoline(
    [](const void* self, u64 x1, u64 x2, u64 x3) -> u64 {
        return decide(Part::Body, x1, g_isOpenBody.orig(self, x1, x2, x3));
    });
HkTrampoline<u64, const void*, u64, u64, u64> g_isOpenTire = hk::hook::trampoline(
    [](const void* self, u64 x1, u64 x2, u64 x3) -> u64 {
        return decide(Part::Tire, x1, g_isOpenTire.orig(self, x1, x2, x3));
    });
HkTrampoline<u64, const void*, u64, u64, u64> g_isOpenWing = hk::hook::trampoline(
    [](const void* self, u64 x1, u64 x2, u64 x3) -> u64 {
        return decide(Part::Wing, x1, g_isOpenWing.orig(self, x1, x2, x3));
    });

template <typename H>
void install(H& hook, Part p) {
    const char* name = kParts[static_cast<int>(p)].symbol;
    const auto off = sym::mainOffset(name);
    if (!off) { MK8AP_LOG_WARN("[unlock] %s unresolved; menu unlock off", name); return; }
    const auto rc = hook.installAtMainOffset(off);
    MK8AP_LOG_INFO("[unlock] %s @+0x%llx: %s", name, static_cast<unsigned long long>(off),
                   rc.succeeded() ? "installed" : "FAILED");
}

}  // namespace

void installPartUnlockHooks() {
    install(g_isOpenDriver, Part::Driver);
    install(g_isOpenBody, Part::Body);
    install(g_isOpenTire, Part::Tire);
    install(g_isOpenWing, Part::Wing);
}

}  // namespace mk8ap::hooks
