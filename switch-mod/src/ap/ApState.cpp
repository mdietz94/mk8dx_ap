#include "ApState.hpp"

#ifndef MK8AP_HOST_TEST
#  include <hk/svc/api.h>
#endif

#include "../util/Log.hpp"

namespace mk8ap::ap {

ApState& ApState::instance() {
    static ApState s;
    return s;
}

std::int64_t ApState::nowMs() {
#ifdef MK8AP_HOST_TEST
    static std::int64_t fake = 0;
    return fake += 16;
#else
    const u64 ticks = hk::svc::getSystemTick();
    return static_cast<std::int64_t>(ticks / 19200ULL);
#endif
}

void ApState::applyState(const State& s) {
    for (int i = 0; i < 4; ++i) drivers[i].store(s.drivers[i], std::memory_order_relaxed);
    for (int i = 0; i < 2; ++i) bodies[i].store(s.bodies[i], std::memory_order_relaxed);
    cups.store(s.cups, std::memory_order_relaxed);
    tires.store(s.tires, std::memory_order_relaxed);
    wings.store(s.wings, std::memory_order_relaxed);
    items.store(s.items, std::memory_order_relaxed);
    speed_pct.store(s.speed_pct, std::memory_order_relaxed);
    engine_max.store(s.engine_max, std::memory_order_relaxed);
    def_driver.store(s.def_driver, std::memory_order_relaxed);
    def_body.store(s.def_body, std::memory_order_relaxed);
    def_tire.store(s.def_tire, std::memory_order_relaxed);
    def_wing.store(s.def_wing, std::memory_order_relaxed);
    cpu_items.store(s.cpu_items, std::memory_order_relaxed);
    state_seq.store(s.seq, std::memory_order_relaxed);
    // Publish `enabled` last (release) so readers that observe it also see
    // the masks above.
    enabled.store(s.enabled, std::memory_order_release);
    MK8AP_LOG_INFO("[state] seq=%d enabled=%d cups=0x%08x items=0x%08x speed=%d%% "
                   "drivers=%08x/%08x/%08x/%08x bodies=%08x/%08x tires=%08x wings=%08x "
                   "def=%d/%d/%d/%d cpu_items=%d",
                   s.seq, s.enabled ? 1 : 0, s.cups, s.items, s.speed_pct,
                   s.drivers[0], s.drivers[1], s.drivers[2], s.drivers[3],
                   s.bodies[0], s.bodies[1], s.tires, s.wings,
                   s.def_driver, s.def_body, s.def_tire, s.def_wing,
                   s.cpu_items ? 1 : 0);
}

namespace {
inline bool bit(std::uint32_t mask, int i) {
    return i >= 0 && i < 32 && ((mask >> i) & 1u);
}
}  // namespace

bool ApState::isCupAllowed(int cupIdx) const {
    if (!restricting()) return true;
    return bit(cups.load(std::memory_order_relaxed), cupIdx);
}
bool ApState::isDriverAllowed(int id) const {
    if (!restricting()) return true;
    if (id < 0 || id >= 128) return true;  // unknown id: never block
    return bit(drivers[id / 32].load(std::memory_order_relaxed), id % 32);
}
bool ApState::isBodyAllowed(int id) const {
    if (!restricting()) return true;
    if (id < 0 || id >= 64) return true;
    return bit(bodies[id / 32].load(std::memory_order_relaxed), id % 32);
}
bool ApState::isTireAllowed(int id) const {
    if (!restricting()) return true;
    if (id < 0 || id >= 32) return true;
    return bit(tires.load(std::memory_order_relaxed), id);
}
bool ApState::isWingAllowed(int id) const {
    if (!restricting()) return true;
    if (id < 0 || id >= 32) return true;
    return bit(wings.load(std::memory_order_relaxed), id);
}
bool ApState::isItemAllowed(int itemSlot) const {
    if (!restricting()) return true;
    if (itemSlot < 0 || itemSlot >= 32) return true;
    return bit(items.load(std::memory_order_relaxed), itemSlot);
}

}  // namespace mk8ap::ap
