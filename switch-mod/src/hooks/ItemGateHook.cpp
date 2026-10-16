// Item restriction.
//
// gear::ItemOwner::startSlot(int slot, EItemSlot item, bool, bool) is the
// single call that hands a rolled item to a kart's item slot (RoGamer97's
// Debug Item Cycler narrates the path: roll -> ItemNumManager limit check ->
// startSlot -> SettleItem UI). Remapping the item argument here changes what
// the roulette lands on, so the visible state stays consistent.
//
// Only locally controlled karts are filtered unless the PC sets cpu_items.
// Locked items become Coin (or None if Coin is locked too).

#include "Hooks.hpp"

#include <cstdint>

#include "hk/hook/Trampoline.h"

#include "../ap/ApState.hpp"
#include "../game/Ids.hpp"
#include "../game/Structs.hpp"
#include "../sym/PatternResolver.hpp"
#include "../util/Log.hpp"

namespace mk8ap::hooks {

namespace {

bool ownerIsLocal(void* owner) {
    if (!owner) return false;
    void* proxy = game::rd<void*>(owner, game::item_owner::kKartInfoProxy);
    if (!proxy) return false;
    void* vehicle = game::rd<void*>(proxy, game::item_owner::kProxyKartVehicle);
    if (!vehicle) return false;
    return game::rd<bool>(vehicle, game::vehicle::kIsMaster);
}

// ABI (4.0.0, 2026-10-16): gear::EItemSlot is a class wrapper passed BY CONST
// REFERENCE, so x2 is a pointer to the 32-bit item id on the caller's stack
// (declaring it `int` truncated the pointer and crashed MainThread at the
// first item roll). x1 (slot) and the two bools are forwarded verbatim; to
// substitute the item we point orig() at a local copy.
using u64 = std::uint64_t;

int derefEnum(u64 reg) {
    if (reg < 0x10000) return -1;
    return *reinterpret_cast<const std::int32_t*>(reg);
}

HkTrampoline<u64, void*, u64, u64, u64, u64> g_startSlot = hk::hook::trampoline(
    [](void* owner, u64 x1, u64 x2, u64 x3, u64 x4) -> u64 {
        static bool logged_sig = false;
        const int item = derefEnum(x2);
        if (!logged_sig) {
            logged_sig = true;
            MK8AP_LOG_INFO("[item] startSlot first call: x1=0x%llx x2=0x%llx (item=%d) x3=0x%llx x4=0x%llx",
                           static_cast<unsigned long long>(x1), static_cast<unsigned long long>(x2), item,
                           static_cast<unsigned long long>(x3), static_cast<unsigned long long>(x4));
        }
        auto& st = ap::ApState::instance();
        if (st.restricting() && item >= 0 && !st.isItemAllowed(item)) {
            const bool local = ownerIsLocal(owner);
            if (local || st.cpu_items.load(std::memory_order_relaxed)) {
                std::int32_t sub = static_cast<std::int32_t>(game::ItemSlot::Coin);
                if (!st.isItemAllowed(sub)) sub = static_cast<std::int32_t>(game::ItemSlot::None);
                MK8AP_LOG_INFO("[item] %s kart rolled locked item %d -> %d",
                               local ? "local" : "cpu", item, sub);
                return g_startSlot.orig(owner, x1, reinterpret_cast<u64>(&sub), x3, x4);
            }
        }
        return g_startSlot.orig(owner, x1, x2, x3, x4);
    });

}  // namespace

void installItemGateHook() {
    const auto off = sym::mainOffset("gear::ItemOwner::startSlot");
    if (!off) { MK8AP_LOG_WARN("[item] ItemOwner::startSlot unresolved; item gating off"); return; }
    const auto rc = g_startSlot.installAtMainOffset(off);
    MK8AP_LOG_INFO("[item] hook @+0x%llx: %s", static_cast<unsigned long long>(off),
                   rc.succeeded() ? "installed" : "FAILED");
}

}  // namespace mk8ap::hooks
