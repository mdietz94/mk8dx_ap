// Pure restriction rules shared by the item and speed hooks. No game or SDK
// dependencies, so tests/test_rules.cpp exercises them on the host.

#pragma once

#include <cstdint>

namespace mk8ap::ap::rules {

// ---- item substitution ------------------------------------------------------
//
// A locked roll used to become Coin outright. The back of the pack mostly
// rolls Star / Bullet / Golden / triple items, so a fresh seed (Mushroom,
// Banana, Coin) handed 10th-12th place a Coin almost every time (player
// report 2026-11). Each item now falls back through weaker items of the same
// kind (speed -> speed, shells -> shells, traps -> traps) before Coin. The
// fallbacks are only the plain box items with no per-race count limit, so a
// substitute never sidesteps the game's ItemNumManager limits (Spiny,
// Lightning, Blooper, Bullet, Star, Golden are never handed out this way).
//
// Ids are gear::EItemSlot (game/Ids.hpp ItemSlot); kept numeric here so the
// header stays dependency-free.
inline constexpr int kItemNone = -1;
inline constexpr int kItemCoin = 15;
inline constexpr int kItemCount = 23;
inline constexpr int kMaxFallbacks = 5;

namespace item {
inline constexpr int Banana = 0, KouraG = 1, KouraR = 2, Kinoko = 3, Kinoko3 = 7,
                     Banana3 = 17, KouraG3 = 18, KouraR3 = 19, End = -2;
}  // namespace item

inline constexpr int kItemFallbacks[kItemCount][kMaxFallbacks] = {
    /*  0 Banana    */ {item::KouraG, item::End},
    /*  1 KouraG    */ {item::Banana, item::End},
    /*  2 KouraR    */ {item::KouraG, item::Banana, item::End},
    /*  3 Kinoko    */ {item::End},
    /*  4 Bomhei    */ {item::KouraR, item::KouraG, item::Banana, item::End},
    /*  5 Gesso     */ {item::Banana3, item::Banana, item::End},
    /*  6 KouraB    */ {item::KouraR3, item::KouraR, item::KouraG3, item::KouraG, item::End},
    /*  7 Kinoko3   */ {item::Kinoko, item::End},
    /*  8 Star      */ {item::Kinoko3, item::Kinoko, item::End},
    /*  9 Killer    */ {item::Kinoko3, item::Kinoko, item::End},
    /* 10 Thunder   */ {item::Kinoko3, item::KouraR3, item::Kinoko, item::KouraR, item::End},
    /* 11 KinokoP   */ {item::Kinoko3, item::Kinoko, item::End},
    /* 12 Fireball  */ {item::KouraG3, item::KouraG, item::End},
    /* 13 Packun    */ {item::KouraR, item::KouraG3, item::KouraG, item::End},
    /* 14 Boomerang */ {item::KouraG3, item::KouraG, item::End},
    /* 15 Coin      */ {item::End},
    /* 16 SHorn     */ {item::KouraR, item::Banana3, item::Banana, item::End},
    /* 17 Banana3   */ {item::Banana, item::End},
    /* 18 KouraG3   */ {item::KouraG, item::KouraR, item::End},
    /* 19 KouraR3   */ {item::KouraR, item::KouraG3, item::KouraG, item::End},
    /* 20 Eight     */ {item::KouraR3, item::Kinoko3, item::Banana3, item::KouraR, item::Kinoko},
    /* 21 Feather   */ {item::End},
    /* 22 Teresa    */ {item::End},
};

// The item a locked roll becomes: the first allowed fallback, else Coin,
// else None. `allowed(int)` is the AP item mask.
template <typename Allowed>
int substituteItem(int locked, Allowed&& allowed) {
    if (locked >= 0 && locked < kItemCount) {
        for (int fb : kItemFallbacks[locked]) {
            if (fb == item::End) break;
            if (allowed(fb)) return fb;
        }
    }
    return allowed(kItemCoin) ? kItemCoin : kItemNone;
}

// ---- speed governor -----------------------------------------------------------
//
// Caps a kart's NORMAL driving speed at top * pct without flattening the
// moments the game throws the kart forward. A hard per-frame clamp to the cap
// (0.2.x) also clamped cannon launches (Sweet Sweet Canyon: the kart crawled
// through the cannon while the field flew past) and every mushroom / boost
// pad. Rules, applied once per frame to the speed the physics produced:
//   * at or under the cap: untouched;
//   * a one-frame jump of more than kLaunchJump * top over the speed we left
//     last frame is a launch (cannon, boost pad, item boost, rocket start):
//     accepted as is;
//   * above the kart's normal top speed (riding a launch out): never allowed
//     to climb, but the game's own deceleration passes through;
//   * between the cap and top: glides down to the cap by kGlideDown * top per
//     frame (about 40 frames from 100% to 60%) instead of snapping.
// Normal acceleration is far below kLaunchJump per frame, so ordinary driving
// still sits exactly at the cap.
inline constexpr float kLaunchJump = 0.04f;
inline constexpr float kGlideDown = 0.01f;

enum class SpeedAction : std::uint8_t { Untouched, Capped, Launch, Hold, Glide };

struct SpeedGovernor {
    float prev = -1.0f;  // speed we left last frame; < 0 = no history (new kart)

    void reset() { prev = -1.0f; }

    // Returns the speed to write back (== speed when the action is
    // Untouched / Launch).
    float step(float speed, float top, int pct, SpeedAction* action = nullptr) {
        SpeedAction a = SpeedAction::Untouched;
        float out = speed;
        const float cap = top * (static_cast<float>(pct) / 100.0f);
        if (pct < 100 && speed > cap) {
            if (prev < 0.0f) {
                a = SpeedAction::Capped;
                out = cap;
            } else if (speed > prev + top * kLaunchJump) {
                a = SpeedAction::Launch;
            } else if (speed > top) {
                a = SpeedAction::Hold;
                out = speed < prev ? speed : prev;
            } else {
                const float glide = prev - top * kGlideDown;
                const float floor = glide > cap ? glide : cap;
                out = speed < floor ? speed : floor;
                a = (floor > cap) ? SpeedAction::Glide : SpeedAction::Capped;
            }
        }
        prev = out;
        if (action) *action = a;
        return out;
    }
};

}  // namespace mk8ap::ap::rules
