// Host test: item substitution and the speed governor (ap/ApRules.hpp).
//   g++ -std=c++20 -Wall -Wextra -DMK8AP_HOST_TEST switch-mod/tests/test_rules.cpp -Iswitch-mod/src

#include <cmath>
#include <cstdint>
#include <cstdio>

#include "ap/ApRules.hpp"

using namespace mk8ap::ap::rules;

static int g_fail = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); ++g_fail; } } while (0)

static bool near(float a, float b) { return std::fabs(a - b) < 1e-4f; }

static auto maskOf(std::uint32_t mask) {
    return [mask](int id) { return id >= 0 && id < 32 && ((mask >> id) & 1u); };
}

static void testItems() {
    // Starting items (Banana 0, Mushroom 3, Coin 15).
    const std::uint32_t start = (1u << 0) | (1u << 3) | (1u << 15);
    CHECK(substituteItem(7, maskOf(start)) == item::Kinoko);    // Triple Mushroom -> Mushroom
    CHECK(substituteItem(8, maskOf(start)) == item::Kinoko);    // Star -> Mushroom
    CHECK(substituteItem(9, maskOf(start)) == item::Kinoko);    // Bullet -> Mushroom
    CHECK(substituteItem(11, maskOf(start)) == item::Kinoko);   // Golden -> Mushroom
    CHECK(substituteItem(20, maskOf(start)) == item::Kinoko);   // Crazy Eight -> Mushroom
    CHECK(substituteItem(18, maskOf(start)) == kItemCoin);      // Triple Green: no shell, no fallback
    CHECK(substituteItem(13, maskOf(start)) == kItemCoin);      // Piranha -> shells only
    CHECK(substituteItem(5, maskOf(start)) == item::Banana);    // Blooper -> Banana
    CHECK(substituteItem(1, maskOf(start)) == item::Banana);    // Green -> Banana

    // First allowed fallback wins, in order.
    const std::uint32_t shells = (1u << 1) | (1u << 2) | (1u << 15);
    CHECK(substituteItem(6, maskOf(shells)) == item::KouraR);   // Spiny -> Red
    CHECK(substituteItem(19, maskOf(shells)) == item::KouraR);  // Triple Red -> Red
    CHECK(substituteItem(18, maskOf(shells)) == item::KouraG);  // Triple Green -> Green
    CHECK(substituteItem(7, maskOf(shells)) == kItemCoin);

    // Coin locked too -> None; unknown ids -> Coin.
    CHECK(substituteItem(8, maskOf(0)) == kItemNone);
    CHECK(substituteItem(99, maskOf(start)) == kItemCoin);
    CHECK(substituteItem(-1, maskOf(start)) == kItemCoin);

    // No fallback ever names an item with a per-race limit, a battle item or
    // the locked item itself.
    for (int i = 0; i < kItemCount; ++i) {
        for (int fb : kItemFallbacks[i]) {
            if (fb == item::End) break;
            CHECK(fb != i);
            CHECK(fb == item::Banana || fb == item::KouraG || fb == item::KouraR ||
                  fb == item::Kinoko || fb == item::Kinoko3 || fb == item::Banana3 ||
                  fb == item::KouraG3 || fb == item::KouraR3);
        }
    }
}

static void testSpeed() {
    const float top = 2.0f;
    const int pct = 60;
    const float cap = 1.2f;
    SpeedAction a;

    // Disabled at 100%.
    SpeedGovernor g;
    CHECK(near(g.step(3.0f, top, 100, &a), 3.0f) && a == SpeedAction::Untouched);

    // No history above the cap: capped at once.
    g.reset();
    CHECK(near(g.step(1.9f, top, pct, &a), cap) && a == SpeedAction::Capped);

    // Ordinary acceleration from a standstill sits at the cap.
    g.reset();
    float v = 0.0f;
    for (int f = 0; f < 300; ++f) v = g.step(v + 0.02f, top, pct, &a);
    CHECK(near(v, cap) && a == SpeedAction::Capped);

    // Cannon: a one-frame jump to 3.0 is not capped, then the game's own
    // speed holds or decays freely while above top.
    CHECK(near(g.step(3.0f, top, pct, &a), 3.0f) && a == SpeedAction::Launch);
    CHECK(near(g.step(3.0f, top, pct, &a), 3.0f));
    CHECK(near(g.step(2.8f, top, pct, &a), 2.8f));
    // ...but may not climb while above top without another launch.
    CHECK(near(g.step(2.85f, top, pct, &a), 2.8f) && a == SpeedAction::Hold);

    // Back under top (the game holds a kart at top): glides down by
    // kGlideDown * top per frame, no snap.
    auto accel = [top](float s) { return s + 0.02f < top ? s + 0.02f : top; };
    v = g.step(2.0f, top, pct, &a);
    CHECK(near(v, 2.0f));
    v = g.step(accel(v), top, pct, &a);
    CHECK(a == SpeedAction::Glide && near(v, 2.0f - top * kGlideDown));
    int frames = 1;
    while (v > cap + 1e-4f && frames < 200) { v = g.step(accel(v), top, pct, &a); ++frames; }
    CHECK(near(v, cap) && frames < 50);

    // Mushroom / mini-turbo: +4.4% of top per frame for many frames stays at
    // the cap (4.0.1 measurement; a 4% threshold let these through).
    g.reset();
    v = g.step(cap, top, pct, &a);
    for (int f = 0; f < 30; ++f) v = g.step(v + top * 0.044f, top, pct, &a);
    CHECK(near(v, cap) && a == SpeedAction::Capped);

    // Dash panel: +12.5% in one frame passes, the ~5% re-sets afterwards glide.
    CHECK(near(g.step(cap + top * 0.125f, top, pct, &a), cap + top * 0.125f) && a == SpeedAction::Launch);
    const float panel = cap + top * 0.125f;
    v = g.step(panel, top, pct, &a);
    CHECK(a == SpeedAction::Glide && near(v, panel - top * kGlideDown));

    // Braking below the cap passes through; speed under the cap is never raised.
    CHECK(near(g.step(0.5f, top, pct, &a), 0.5f) && a == SpeedAction::Untouched);
    CHECK(near(g.step(-0.3f, top, pct, &a), -0.3f));
}

int main() {
    testItems();
    testSpeed();
    if (g_fail == 0) std::printf("test_rules: all passed\n");
    return g_fail == 0 ? 0 : 1;
}
