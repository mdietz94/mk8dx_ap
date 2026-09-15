// Character / vehicle enforcement + course-id learning.
//
// ui::SetNextCourse(const CourseInfo&) is the commit point of the menu flow
// (driver -> kart -> course). At that moment gear::GetNextRaceInfo() holds
// every kart's driver/body/tire/glider ids, so:
//   * any locally controlled kart whose selection is AP-locked is rewritten
//     to the seed's starting loadout (the select screen is not greyed yet;
//     that needs a Page_DriverSelect hook nobody has published);
//   * the CourseInfo gives us (course id, icon resource name) to teach the
//     PC which id is which course.
//
// "Local" karts: kart 0 is always the first local player; further local
// players share its RacePlayerType (CPUs differ), so we treat every kart
// whose player type equals kart 0's as local. CPUs are never rewritten.

#include "Hooks.hpp"

#include <cstdint>

#include "hk/hook/Trampoline.h"

#include "../ap/ApFrameBridge.hpp"
#include "../ap/ApState.hpp"
#include "../game/GameApi.hpp"
#include "../game/Structs.hpp"
#include "../sym/PatternResolver.hpp"
#include "../util/Log.hpp"

namespace mk8ap::hooks {

namespace {

void enforceLoadouts() {
    auto& st = ap::ApState::instance();
    if (!st.restricting()) return;
    void* ri = game::getNextRaceInfo();
    if (!ri) { MK8AP_LOG_WARN("[kart] GetNextRaceInfo() null; cannot enforce"); return; }
    const int n = static_cast<int>(game::rd<std::uint32_t>(ri, game::race_info::kPlayerAmount));
    const int count = (n > 0 && n <= game::race_info::kMaxKarts) ? n : game::race_info::kMaxKarts;
    const int localType = game::rd<std::int32_t>(game::kartInfoAt(ri, 0), game::kart_info::kPlayerType);
    for (int i = 0; i < count; ++i) {
        void* ki = game::kartInfoAt(ri, i);
        if (game::rd<std::int32_t>(ki, game::kart_info::kPlayerType) != localType) continue;
        const int drv = game::rd<std::int32_t>(ki, game::kart_info::kDriverId);
        const int body = game::rd<std::int32_t>(ki, game::kart_info::kBodyId);
        const int tire = game::rd<std::int32_t>(ki, game::kart_info::kTireId);
        const int wing = game::rd<std::int32_t>(ki, game::kart_info::kGliderId);
        if (!st.isDriverAllowed(drv)) {
            const int d = st.def_driver.load();
            MK8AP_LOG_INFO("[kart] kart %d driver %d locked -> %d", i, drv, d);
            game::wr<std::int32_t>(ki, game::kart_info::kDriverId, d);
            game::wr<std::uint8_t>(ki, game::kart_info::kDriverVariant, 0);
        }
        if (!st.isBodyAllowed(body)) {
            const int d = st.def_body.load();
            MK8AP_LOG_INFO("[kart] kart %d body %d locked -> %d", i, body, d);
            game::wr<std::int32_t>(ki, game::kart_info::kBodyId, d);
        }
        if (!st.isTireAllowed(tire)) {
            const int d = st.def_tire.load();
            MK8AP_LOG_INFO("[kart] kart %d tire %d locked -> %d", i, tire, d);
            game::wr<std::int32_t>(ki, game::kart_info::kTireId, d);
        }
        if (!st.isWingAllowed(wing)) {
            const int d = st.def_wing.load();
            MK8AP_LOG_INFO("[kart] kart %d glider %d locked -> %d", i, wing, d);
            game::wr<std::int32_t>(ki, game::kart_info::kGliderId, d);
        }
    }
}

HkTrampoline<std::uint64_t, const void*> g_setNextCourse = hk::hook::trampoline(
    [](const void* courseInfo) -> std::uint64_t {
        if (courseInfo) {
            const int id = game::rd<std::int32_t>(courseInfo, game::course_info::kCourseId);
            const char* res = game::courseInfoIconName(courseInfo);
            MK8AP_LOG_INFO("[kart] SetNextCourse id=%d res=%s", id, res ? res : "(null)");
            ap::reportCourseName(id, res);
        }
        enforceLoadouts();
        return g_setNextCourse.orig(courseInfo);
    });

}  // namespace

void installKartGuardHook() {
    const auto off = sym::mainOffset("ui::SetNextCourse");
    if (!off) { MK8AP_LOG_WARN("[kart] ui::SetNextCourse unresolved; loadout guard off"); return; }
    const auto rc = g_setNextCourse.installAtMainOffset(off);
    MK8AP_LOG_INFO("[kart] hook @+0x%llx: %s", static_cast<unsigned long long>(off),
                   rc.succeeded() ? "installed" : "FAILED");
}

}  // namespace mk8ap::hooks
