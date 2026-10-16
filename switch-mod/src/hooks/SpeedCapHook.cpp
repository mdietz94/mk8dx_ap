// Top-speed cap.
//
// ui::Control_RaceLapCoin::onCalc_ runs every frame while the race HUD is
// up (it is the hook site fishguy6564's speedometer example uses). Each
// frame we walk to every locally controlled kart's KartVehicleMove and clamp
// its current speed to top_speed * speed_pct. Clamping mSpeed (rather than
// rewriting the top-speed fields the game may recompute) is robust to
// evaluation order: whatever the physics produced, the value that carries
// into the next frame is capped.
//
// The KartVehicleMove offsets are flagged "still based off of 32-bit
// structure" upstream, so the first frame logs the raw floats and the clamp
// stays disabled unless they look sane (0 < top < 100).

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

bool g_logged_raw = false;
bool g_offsets_sane = false;

void clampKart(int idx, int pct) {
    void* vehicle = game::kartVehicle(idx);
    if (!vehicle) return;
    if (!game::rd<bool>(vehicle, game::vehicle::kIsMaster)) return;
    void* move = game::rd<void*>(vehicle, game::scene::kVehicleMove);
    if (!move) return;
    const float speed = game::rd<float>(move, game::vehicle_move::kSpeed);
    const float top1 = game::rd<float>(move, game::vehicle_move::kTopSpeed1);
    const float top2 = game::rd<float>(move, game::vehicle_move::kTopSpeed2);
    const float top3 = game::rd<float>(move, game::vehicle_move::kTopSpeed3);
    if (!g_logged_raw) {
        g_logged_raw = true;
        g_offsets_sane = (top1 > 0.0f && top1 < 100.0f && speed > -100.0f && speed < 100.0f);
        MK8AP_LOG_INFO("[speed] kart %d raw: speed=%.3f top=%.3f/%.3f/%.3f -> offsets %s",
                       idx, speed, top1, top2, top3, g_offsets_sane ? "look sane" : "SUSPECT (clamp disabled)");
    }
    if (!g_offsets_sane || pct >= 100) return;
    const float cap = top1 * (static_cast<float>(pct) / 100.0f);
    if (speed > cap) game::wr<float>(move, game::vehicle_move::kSpeed, cap);
}

HkTrampoline<std::uint64_t, void*> g_onCalc = hk::hook::trampoline(
    [](void* self) -> std::uint64_t {
        startNetworkingOnce("ui::Control_RaceLapCoin::onCalc_");
        auto& st = ap::ApState::instance();
        const int pct = st.speed_pct.load(std::memory_order_relaxed);
        if (st.restricting() && pct < 100) {
            static bool logged_live = false;
            if (!logged_live) {
                logged_live = true;
                void* ri = game::getRaceInfo();
                void* ki = ri ? game::kartInfoAt(ri, game::localKartIndex()) : nullptr;
                MK8AP_LOG_INFO("[speed] clamping to %d%%; live RaceInfo kart0 driver=%d body=%d tire=%d glider=%d",
                               pct,
                               ki ? game::rd<std::int32_t>(ki, game::kart_info::kDriverId) : -1,
                               ki ? game::rd<std::int32_t>(ki, game::kart_info::kBodyId) : -1,
                               ki ? game::rd<std::int32_t>(ki, game::kart_info::kTireId) : -1,
                               ki ? game::rd<std::int32_t>(ki, game::kart_info::kGliderId) : -1);
            }
            for (int i = 0; i < game::race_info::kMaxKarts; ++i) clampKart(i, pct);
        } else if (!g_logged_raw) {
            clampKart(game::localKartIndex(), 100);  // log raw values once anyway
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
