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

HkTrampoline<std::uint64_t, void*, int, int, bool, bool> g_startSlot = hk::hook::trampoline(
    [](void* owner, int slot, int item, bool a, bool b) -> std::uint64_t {
        auto& st = ap::ApState::instance();
        if (st.restricting() && item >= 0 && !st.isItemAllowed(item)) {
            const bool local = ownerIsLocal(owner);
            if (local || st.cpu_items.load(std::memory_order_relaxed)) {
                int sub = static_cast<int>(game::ItemSlot::Coin);
                if (!st.isItemAllowed(sub)) sub = static_cast<int>(game::ItemSlot::None);
                MK8AP_LOG_INFO("[item] %s kart rolled locked item %d -> %d",
                               local ? "local" : "cpu", item, sub);
                item = sub;
            }
        }
        return g_startSlot.orig(owner, slot, item, a, b);
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
