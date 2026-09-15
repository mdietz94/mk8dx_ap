// ui::RaceWindow::onFakeGoal(): fires when the FINISH! banner shows for a
// race window. Fallback race-result channel (the PlayReport capture is the
// primary one); the PC dedupes the two by course + time.
//
// Derivation (RoGamer97 "YOU WIN / YOU LOSE" patch, 3.0.5): the finishing
// kart's index = getMyKartIndex() (-1 offline -> 0) + window id, where the
// window id is the first int of the RaceWindow object; its rank is
// RaceKartChecker[idx] + 0x40 via the scene -> ObjectEngine -> RaceDirector
// chain.

#include "Hooks.hpp"

#include <cstdint>

#include "hk/hook/Trampoline.h"

#include "../ap/ApFrameBridge.hpp"
#include "../game/GameApi.hpp"
#include "../game/Ids.hpp"
#include "../game/Structs.hpp"
#include "../sym/PatternResolver.hpp"
#include "../util/Log.hpp"

namespace mk8ap::hooks {

namespace {

HkTrampoline<std::uint64_t, void*> g_onFakeGoal = hk::hook::trampoline(
    [](void* window) -> std::uint64_t {
        const int window_id = window ? game::rd<std::int32_t>(window, 0) : 0;
        const int kart = game::localKartIndex() + window_id;
        const int rank = game::kartRank(kart);
        const int rule = game::currentRule();
        const int engine = game::currentEngineClass();
        const int course = game::currentCourseId();
        const bool mirror = game::currentIsMirror();
        MK8AP_LOG_INFO("[finish] onFakeGoal window=%d kart=%d rank=%d course=%d rule=%d engine=%d mirror=%d",
                       window_id, kart, rank, course, rule, engine, mirror ? 1 : 0);
        ap::reportRaceResult(course, rule, engine, mirror, rank, kart);
        return g_onFakeGoal.orig(window);
    });

}  // namespace

void installRaceFinishHook() {
    const auto off = sym::mainOffset("ui::RaceWindow::onFakeGoal");
    if (!off) {
        MK8AP_LOG_WARN("[finish] onFakeGoal unresolved; relying on PlayReport capture only");
        return;
    }
    const auto rc = g_onFakeGoal.installAtMainOffset(off);
    MK8AP_LOG_INFO("[finish] hook @+0x%llx: %s", static_cast<unsigned long long>(off),
                   rc.succeeded() ? "installed" : "FAILED");
}

}  // namespace mk8ap::hooks
