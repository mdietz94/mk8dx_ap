// Character / vehicle enforcement.
//
// ui::SetNextCourse(const CourseInfo&) is the commit point of the menu flow
// (driver -> kart -> course). At that moment gear::GetNextRaceInfo() holds
// every kart's driver/body/tire/glider ids, so:
//   * any locally controlled kart whose selection is AP-locked is rewritten
//     to the seed's starting loadout (the select screen is not greyed yet;
//     that needs a Page_DriverSelect hook nobody has published).
//
// "Local" karts: kart 0 is always the first local player; further local
// players share its RacePlayerType (CPUs differ), so we treat every kart
// whose player type equals kart 0's as local. CPUs are never rewritten.

#include "Hooks.hpp"

#include <cstdint>

#include "hk/hook/Trampoline.h"

#include "../ap/ApState.hpp"
#include "../game/GameApi.hpp"
#include "../game/Structs.hpp"
#include "../sym/PatternResolver.hpp"
#include "../util/Log.hpp"

namespace mk8ap::hooks {

namespace {

void enforceIn(void* ri, const char* which) {
    auto& st = ap::ApState::instance();
    if (!ri) { MK8AP_LOG_WARN("[kart] %s RaceInfo null; cannot enforce", which); return; }
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
        if (i == 0) {
            MK8AP_LOG_INFO("[kart] %s kart0 driver=%d body=%d tire=%d glider=%d type=%d n=%d",
                           which, drv, body, tire, wing, localType, n);
        }
        if (!st.isDriverAllowed(drv)) {
            const int d = st.def_driver.load();
            MK8AP_LOG_INFO("[kart] %s kart %d driver %d locked -> %d", which, i, drv, d);
            game::wr<std::int32_t>(ki, game::kart_info::kDriverId, d);
            game::wr<std::uint8_t>(ki, game::kart_info::kDriverVariant, 0);
        }
        if (!st.isBodyAllowed(body)) {
            const int d = st.def_body.load();
            MK8AP_LOG_INFO("[kart] %s kart %d body %d locked -> %d", which, i, body, d);
            game::wr<std::int32_t>(ki, game::kart_info::kBodyId, d);
        }
        if (!st.isTireAllowed(tire)) {
            const int d = st.def_tire.load();
            MK8AP_LOG_INFO("[kart] %s kart %d tire %d locked -> %d", which, i, tire, d);
            game::wr<std::int32_t>(ki, game::kart_info::kTireId, d);
        }
        if (!st.isWingAllowed(wing)) {
            const int d = st.def_wing.load();
            MK8AP_LOG_INFO("[kart] %s kart %d glider %d locked -> %d", which, i, wing, d);
            game::wr<std::int32_t>(ki, game::kart_info::kGliderId, d);
        }
    }
}

// First real seed (2026-10-16): writes to GetNextRaceInfo() at SetNextCourse
// did not change the race (PlayReport still named the menu choice), so the
// current RaceInfo is rewritten too and both are logged to find out which
// one the race actually reads.
void enforceLoadouts() {
    auto& st = ap::ApState::instance();
    if (!st.restricting()) return;
    enforceIn(game::getNextRaceInfo(), "next");
    enforceIn(game::getRaceInfo(), "cur");
}

HkTrampoline<std::uint64_t, const void*> g_setNextCourse = hk::hook::trampoline(
    [](const void* courseInfo) -> std::uint64_t {
        startNetworkingOnce("ui::SetNextCourse");
        if (courseInfo) {
            MK8AP_LOG_INFO("[kart] SetNextCourse course id=%d dlc=%d",
                           game::rd<std::int32_t>(courseInfo, game::course_info::kCourseId),
                           game::rd<bool>(courseInfo, game::course_info::kIsDLC) ? 1 : 0);
        }
        enforceLoadouts();
        return g_setNextCourse.orig(courseInfo);
    });

// ui::SetRandomCPU(bool, bool, sead::SafeArray<EDriverID,12>*) runs at race
// setup, after the player's selection has been copied into RaceInfo and
// while the CPU combos are being decided (RoGamer97's CPU Combo Modifier
// hooks inside it). Enforcing after it returns catches the final table even
// when SetNextCourse is not called a second time.
HkTrampoline<std::uint64_t, std::uint64_t, std::uint64_t, std::uint64_t, std::uint64_t> g_setRandomCpu =
    hk::hook::trampoline([](std::uint64_t x0, std::uint64_t x1, std::uint64_t x2,
                            std::uint64_t x3) -> std::uint64_t {
        const std::uint64_t ret = g_setRandomCpu.orig(x0, x1, x2, x3);
        MK8AP_LOG_INFO("[kart] SetRandomCPU(%d, %d) done; enforcing loadouts",
                       static_cast<int>(x0 & 1), static_cast<int>(x1 & 1));
        enforceLoadouts();
        return ret;
    });

// object::KartDirector::prepareBeforeStructure(const gear::ArgumentObj*) is
// the race scene's kart-director prepare: by now the UI has assembled the
// final 12-kart RaceInfo (seen at SetNextCourse(28) time with n=12) and the
// kart objects have not been built yet. Enforcing here, pre-orig, is the
// "catch it upstream of the visible state change" point. The earlier hooks
// (SetNextCourse / SetRandomCPU) all ran before the UI's final copy and had
// no effect on the race (2026-10-16, race 3: PlayReport still BbLuigi/K_Snk).
HkTrampoline<std::uint64_t, void*, std::uint64_t, std::uint64_t, std::uint64_t> g_kartPrepare =
    hk::hook::trampoline([](void* self, std::uint64_t x1, std::uint64_t x2,
                            std::uint64_t x3) -> std::uint64_t {
        MK8AP_LOG_INFO("[kart] KartDirector::prepareBeforeStructure: enforcing loadouts pre-orig");
        enforceLoadouts();
        return g_kartPrepare.orig(self, x1, x2, x3);
    });

}  // namespace

void installKartGuardHook() {
    if (const auto off3 = sym::mainOffset("object::KartDirector::prepareBeforeStructure")) {
        const auto rc3 = g_kartPrepare.installAtMainOffset(off3);
        MK8AP_LOG_INFO("[kart] KartDirector::prepareBeforeStructure hook @+0x%llx: %s",
                       static_cast<unsigned long long>(off3), rc3.succeeded() ? "installed" : "FAILED");
    } else {
        MK8AP_LOG_WARN("[kart] KartDirector::prepareBeforeStructure unresolved; scene-level enforcement off");
    }
    if (const auto off2 = sym::mainOffset("ui::SetRandomCPU")) {
        const auto rc2 = g_setRandomCpu.installAtMainOffset(off2);
        MK8AP_LOG_INFO("[kart] SetRandomCPU hook @+0x%llx: %s", static_cast<unsigned long long>(off2),
                       rc2.succeeded() ? "installed" : "FAILED");
    } else {
        MK8AP_LOG_WARN("[kart] ui::SetRandomCPU unresolved; race-setup enforcement off");
    }
    const auto off = sym::mainOffset("ui::SetNextCourse");
    if (!off) { MK8AP_LOG_WARN("[kart] ui::SetNextCourse unresolved; loadout guard off"); return; }
    const auto rc = g_setNextCourse.installAtMainOffset(off);
    MK8AP_LOG_INFO("[kart] hook @+0x%llx: %s", static_cast<unsigned long long>(off),
                   rc.succeeded() ? "installed" : "FAILED");
}

}  // namespace mk8ap::hooks
