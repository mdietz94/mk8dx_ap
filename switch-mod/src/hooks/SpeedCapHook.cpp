// Top-speed cap.
//
// ui::Control_RaceLapCoin::onCalc_ runs every frame while the race HUD is
// up (it is the hook site fishguy6564's speedometer example uses). Each
// frame we walk to every locally controlled kart's KartVehicleMove and run
// its current speed through ap::rules::SpeedGovernor (normal driving capped
// at top_speed * speed_pct; cannon / boost launches pass, see ApRules.hpp).
// Writing mSpeed (rather than the top-speed fields the game may recompute)
// is robust to evaluation order: whatever the physics produced, the value
// that carries into the next frame is governed.
//
// The KartVehicleMove offsets are flagged "still based off of 32-bit
// structure" upstream, so every frame is sanity-checked (0 < top < 100) and
// a frame that fails is skipped. 0.2.1 latched the FIRST frame's verdict for
// the whole session: a first frame read before the kart was set up (top = 0)
// left the cap off until the game restarted (player report 2026-11: client
// connected before the emulator started, 60% listed, no cap in races).

#include "Hooks.hpp"

#include <cstdint>

#include "hk/hook/Trampoline.h"

#include "../ap/ApRules.hpp"
#include "../ap/ApState.hpp"
#include "../game/GameApi.hpp"
#include "../game/Structs.hpp"
#include "../sym/PatternResolver.hpp"
#include "../util/Log.hpp"

namespace mk8ap::hooks {

namespace {

using ap::rules::SpeedAction;
using ap::rules::SpeedGovernor;

struct KartSlot {
    void* vehicle = nullptr;
    SpeedGovernor gov;
};
KartSlot g_karts[game::race_info::kMaxKarts];

bool g_logged_sane = false;
int g_suspect_logs = 0;
bool g_logged_engaged = false;
int g_launch_logs = 0;

bool looksSane(float speed, float top) {
    return top > 0.0f && top < 100.0f && speed > -100.0f && speed < 100.0f;
}

void governKart(int idx, int pct) {
    if (idx < 0 || idx >= game::race_info::kMaxKarts) return;
    KartSlot& slot = g_karts[idx];
    void* vehicle = game::kartVehicle(idx);
    if (vehicle != slot.vehicle) {  // new race / rebuilt kart: drop history
        slot.vehicle = vehicle;
        slot.gov.reset();
    }
    if (!vehicle) return;
    if (!game::rd<bool>(vehicle, game::vehicle::kIsMaster)) return;
    void* move = game::rd<void*>(vehicle, game::scene::kVehicleMove);
    if (!move) return;
    const float speed = game::rd<float>(move, game::vehicle_move::kSpeed);
    const float top1 = game::rd<float>(move, game::vehicle_move::kTopSpeed1);
    if (!looksSane(speed, top1)) {
        slot.gov.reset();
        if (g_suspect_logs < 5) {
            ++g_suspect_logs;
            MK8AP_LOG_INFO("[speed] kart %d raw: speed=%.3f top=%.3f/%.3f/%.3f -> SUSPECT, frame skipped",
                           idx, speed, top1, game::rd<float>(move, game::vehicle_move::kTopSpeed2),
                           game::rd<float>(move, game::vehicle_move::kTopSpeed3));
        }
        return;
    }
    if (!g_logged_sane) {
        g_logged_sane = true;
        MK8AP_LOG_INFO("[speed] kart %d raw: speed=%.3f top=%.3f/%.3f/%.3f -> offsets look sane",
                       idx, speed, top1, game::rd<float>(move, game::vehicle_move::kTopSpeed2),
                       game::rd<float>(move, game::vehicle_move::kTopSpeed3));
    }
    if (pct >= 100) { slot.gov.reset(); return; }
    const float prev = slot.gov.prev;
    SpeedAction action;
    const float out = slot.gov.step(speed, top1, pct, &action);
    if (action == SpeedAction::Launch && g_launch_logs < 30) {
        ++g_launch_logs;
        MK8AP_LOG_INFO("[speed] kart %d launch: %.3f -> %.3f (top %.3f, cap %d%%), not capped",
                       idx, prev, speed, top1, pct);
    }
    if (out != speed) {
        if (!g_logged_engaged) {
            g_logged_engaged = true;
            MK8AP_LOG_INFO("[speed] cap engaged: kart %d %.3f -> %.3f (top %.3f, %d%%)",
                           idx, speed, out, top1, pct);
        }
        game::wr<float>(move, game::vehicle_move::kSpeed, out);
    }
}

HkTrampoline<std::uint64_t, void*> g_onCalc = hk::hook::trampoline(
    [](void* self) -> std::uint64_t {
        startNetworkingOnce("ui::Control_RaceLapCoin::onCalc_");
        auto& st = ap::ApState::instance();
        const int pct = st.speed_pct.load(std::memory_order_relaxed);
        if (st.restricting() && pct < 100) {
            static int logged_pct = -1;
            if (logged_pct != pct) {
                logged_pct = pct;
                void* ri = game::getRaceInfo();
                void* ki = ri ? game::kartInfoAt(ri, game::localKartIndex()) : nullptr;
                MK8AP_LOG_INFO("[speed] clamping to %d%%; live RaceInfo kart0 driver=%d body=%d tire=%d glider=%d",
                               pct,
                               ki ? game::rd<std::int32_t>(ki, game::kart_info::kDriverId) : -1,
                               ki ? game::rd<std::int32_t>(ki, game::kart_info::kBodyId) : -1,
                               ki ? game::rd<std::int32_t>(ki, game::kart_info::kTireId) : -1,
                               ki ? game::rd<std::int32_t>(ki, game::kart_info::kGliderId) : -1);
            }
            for (int i = 0; i < game::race_info::kMaxKarts; ++i) governKart(i, pct);
        } else if (!g_logged_sane) {
            governKart(game::localKartIndex(), 100);  // log raw values once anyway
        }
        return g_onCalc.orig(self);
    });

}  // namespace

void installSpeedCapHook() {
    const auto off = sym::mainOffset("ui::Control_RaceLapCoin::onCalc_");
    if (!off) { MK8AP_LOG_WARN("[speed] Control_RaceLapCoin::onCalc_ unresolved; speed cap off"); return; }
    const auto rc = g_onCalc.installAtMainOffset(off);
    MK8AP_LOG_INFO("[speed] hook @+0x%llx: %s", static_cast<unsigned long long>(off),
                   rc.succeeded() ? "installed" : "FAILED");
}

}  // namespace mk8ap::hooks
