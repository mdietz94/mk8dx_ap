// Stat cap: the local kart races as if it were built from weak parts.
//
// Instead of clamping the speed every frame (SpeedCapHook, speed_restriction
// = top_speed_cap), stat_cap mode lowers the kart's STATS before the game
// derives its physics from them, so speed, acceleration, handling, traction,
// mini-turbo, weight and invincibility are all those of a worse combo while
// boosts, cannons and items behave exactly as the game intends.
//
// Where the stats come from (4.0.1, static analysis 2026-11-09):
//   main+0x170738  (called only from +0x16FA58 during kart setup, three sites)
//     x0 = object::KartVehicle*, x1 = parameter object, w2 = bool, s0 = float
//   reads the global stat tables through the GOT slot main+0x12F9D98:
//     *(*(main+0x12F9D98)) = manager, manager+0xF0 = table object `tbl`;
//     tbl+0x20 must be non-null, then per part (row = 14 s32, 0x38 bytes):
//       body   rows tbl+0x98  count u16 tbl+0xD4  fallback row tbl+0x1B8
//       driver rows tbl+0xA0  count u16 tbl+0xD6  fallback row tbl+0x1F0
//       tire   rows tbl+0xA8  count u16 tbl+0xD8  fallback row tbl+0x228
//       glider rows tbl+0xB0  count u16 tbl+0xDA  fallback row tbl+0x260
//   with the part ids at KartVehicle+0xB4 (body) / +0xB8 (driver) / +0xC0
//   (tire) / +0xC4 (glider). Mii drivers (0x1D, 0x34) with u32 v+0xFC <= 2
//   use driver row s32[main+0xF242C8 + 4 * v+0xFC] instead.
//   For each of the 14 columns the four rows are summed into KartVehicle
//   +0x2DC + 4 * col and the level = clamp(sum - 1, 0, 19) indexes the
//   per-level physics tables (weight -> Move+0x3F8.., speed -> top speeds,
//   handling -> FUN_1860f8...). CPUs (v+0xD2) are forced to level 20 on
//   several stats by the same function.
//
// The hook, for a non-CPU kart while an AP slot restricts:
//  * stat_cap < 20: compute the same sums the game will, subtract each
//    column's excess over the cap from the BODY row the game is about to
//    read, call the original, and put the row back.
//  * base_speed_pct < 100: after the original, scale the base top speeds it
//    copied into KartVehicleMove. The game's speed stat only spans ~6% from
//    level 0 to 19 (7.525 -> 8.000 ground), so the cap alone is barely felt;
//    acceleration spans 0.035 -> 0.130 and is felt. The tables are shared (the selection screen reads them too),
// so the patch lives only for the duration of the original call; kart setup
// runs on one thread. CPUs are never touched.
//
// Safety: every pointer is range-checked and the row pointers must lie
// outside main's image (the tables are loaded into the heap; writing into
// main's read-only data would fault). Any check failing = vanilla call.

#include "Hooks.hpp"

#include <atomic>
#include <cstdint>

#include "hk/hook/Trampoline.h"

#include "../ap/ApRules.hpp"
#include "../ap/ApState.hpp"
#include "../game/Structs.hpp"
#include "../sym/PatternResolver.hpp"
#include "../util/Log.hpp"

namespace mk8ap::hooks {

namespace {

using u64 = std::uint64_t;
namespace rules = ap::rules;

constexpr const char* kSymbol = "object::KartVehicle::setupStatParam";

// main-relative data addresses on 4.0.1 (see header comment).
constexpr std::uintptr_t kStatGotSlot = 0x12F9D98;
constexpr std::uintptr_t kMiiDriverMap = 0xF242C8;
constexpr std::uintptr_t kMainImageSpan = 0x2000000;  // main .text..bss upper bound

// KartVehicle fields.
constexpr std::size_t kBodyId = 0xB4, kDriverId = 0xB8, kTireId = 0xC0, kWingId = 0xC4;
constexpr std::size_t kPlayerIdx = 0xA8, kMiiForm = 0xFC;
constexpr int kMiiDriverA = 0x1D, kMiiDriverB = 0x34;

struct PartTable {
    const char* label;
    std::size_t rows, count, fallback;
};
constexpr PartTable kTables[4] = {
    {"body", 0x98, 0xD4, 0x1B8},
    {"driver", 0xA0, 0xD6, 0x1F0},
    {"tire", 0xA8, 0xD8, 0x228},
    {"glider", 0xB0, 0xDA, 0x260},
};
constexpr std::size_t kRowBytes = 0x38;

std::uintptr_t g_mainBase = 0;
std::atomic<int> g_logged{0};

bool looksLikePtr(u64 p) { return p >= 0x10000 && (p & 0x3) == 0; }
bool inMain(u64 p) { return g_mainBase && p >= g_mainBase && p < g_mainBase + kMainImageSpan; }

template <typename T>
T rd(u64 base, std::size_t off) { return *reinterpret_cast<const T*>(base + off); }

u64 statTable() {
    if (!g_mainBase) return 0;
    const u64 global = rd<u64>(g_mainBase, kStatGotSlot);
    if (!looksLikePtr(global)) return 0;
    const u64 mgr = rd<u64>(global, 0);
    if (!looksLikePtr(mgr)) return 0;
    const u64 tbl = rd<u64>(mgr, 0xF0);
    if (!looksLikePtr(tbl) || inMain(tbl)) return 0;
    return tbl;
}

// The row the game will read for part `k` with id `id`, mirroring its
// fallback rule exactly.
std::int32_t* rowFor(u64 tbl, int k, int id) {
    const auto& t = kTables[k];
    const u64 rows = rd<u64>(tbl, t.rows);
    const int count = rd<std::uint16_t>(tbl, t.count);
    if (rd<u64>(tbl, 0x20) == 0 || rows == 0 || id < 0 || id >= count)
        return reinterpret_cast<std::int32_t*>(tbl + t.fallback);
    if (!looksLikePtr(rows) || inMain(rows)) return nullptr;
    return reinterpret_cast<std::int32_t*>(rows + static_cast<u64>(id) * kRowBytes);
}

int driverRowIndex(u64 v) {
    const int driver = rd<std::int32_t>(v, kDriverId);
    if (driver != kMiiDriverA && driver != kMiiDriverB) return driver;
    const std::uint32_t form = rd<std::uint32_t>(v, kMiiForm);
    if (form > 2) return driver;
    return rd<std::int32_t>(g_mainBase, kMiiDriverMap + 4 * form);
}

// Per-level physics tables (tbl+0x28..+0x70, count u16 at tbl+0xB8..+0xCA),
// logged once per session so the effect of a level drop can be measured:
//   +0x28 weight (3 floats) -> Move+0x3F8..   +0x30 (2 floats) -> Move+0x4D8/+0x4D0
//   +0x58 / +0x60 / +0x68 speed ground / water / anti-grav (2 floats) -> Move+0x4B8..+0x4CC
//   +0x70 speed air (1 float) -> Move+0x50C
std::atomic<bool> g_levelsLogged{false};

float level(u64 tbl, std::size_t ptrOff, std::size_t cntOff, int stride, int lvl, int idx) {
    const u64 p = rd<u64>(tbl, ptrOff);
    const int n = rd<std::uint16_t>(tbl, cntOff);
    if (!looksLikePtr(p) || lvl >= n) return -1.0f;
    return *reinterpret_cast<const float*>(p + static_cast<u64>(lvl) * stride + 4 * idx);
}

void logLevelTablesOnce(u64 tbl) {
    if (g_levelsLogged.exchange(true)) return;
    MK8AP_LOG_INFO("[statlvl] counts w%d a%d s%d/%d/%d/%d", rd<std::uint16_t>(tbl, 0xB8),
                   rd<std::uint16_t>(tbl, 0xBA), rd<std::uint16_t>(tbl, 0xC4), rd<std::uint16_t>(tbl, 0xC6),
                   rd<std::uint16_t>(tbl, 0xC8), rd<std::uint16_t>(tbl, 0xCA));
    for (int l = 0; l <= 20; ++l) {
        MK8AP_LOG_INFO("[statlvl] L%2d w %.4f %.4f %.4f a30 %.4f %.4f sG %.4f %.4f sW %.4f %.4f sA %.4f %.4f sAir %.4f",
                       l, level(tbl, 0x28, 0xB8, 0xC, l, 0), level(tbl, 0x28, 0xB8, 0xC, l, 1),
                       level(tbl, 0x28, 0xB8, 0xC, l, 2), level(tbl, 0x30, 0xBA, 8, l, 0),
                       level(tbl, 0x30, 0xBA, 8, l, 1), level(tbl, 0x58, 0xC4, 8, l, 0),
                       level(tbl, 0x58, 0xC4, 8, l, 1), level(tbl, 0x60, 0xC6, 8, l, 0),
                       level(tbl, 0x60, 0xC6, 8, l, 1), level(tbl, 0x68, 0xC8, 8, l, 0),
                       level(tbl, 0x68, 0xC8, 8, l, 1), level(tbl, 0x70, 0xCA, 4, l, 0));
    }
}

// Base top speeds the game copied from the speed level tables into
// KartVehicleMove (ground / water / anti-grav: 0 coins, 10 coins; air).
// FUN_182C14 rebuilds the live top speed every frame from these
// (Move+0x420 = lerp(+0x4B8, +0x4BC, coins / 10) * Move+0x4B4 engine factor)
// and boost / star speeds are that top times a multiplier, so scaling them
// here slows the kart the way a worse speed stat would, boosts included,
// without touching any frame.
constexpr std::size_t kMove = 0x28;
constexpr std::size_t kBaseSpeeds[] = {0x4B8, 0x4BC, 0x4C0, 0x4C4, 0x4C8, 0x4CC, 0x50C};

bool scaleBaseSpeed(u64 v, int pct) {
    const u64 move = rd<u64>(v, kMove);
    if (!looksLikePtr(move) || inMain(move)) return false;
    for (std::size_t off : kBaseSpeeds) {
        const float f = rd<float>(move, off);
        if (!(f > 1.0f && f < 50.0f)) return false;  // not the values we expect: leave all alone
    }
    const float k = static_cast<float>(pct) / 100.0f;
    for (std::size_t off : kBaseSpeeds) *reinterpret_cast<float*>(move + off) = rd<float>(move, off) * k;
    return true;
}

HkTrampoline<u64, u64, u64, u64, u64, u64, u64, float> g_setupStat = hk::hook::trampoline(
    [](u64 v, u64 x1, u64 x2, u64 x3, u64 x4, u64 x5, float s0) -> u64 {
        const auto& st = ap::ApState::instance();
        const int cap = st.stat_cap.load(std::memory_order_relaxed);
        const int pct = st.base_speed_pct.load(std::memory_order_relaxed);
        if (!st.restricting() || (cap >= rules::kStatMax && pct >= 100) || !looksLikePtr(v) ||
            rd<std::uint8_t>(v, game::vehicle::kIsCpu) != 0)
            return g_setupStat.orig(v, x1, x2, x3, x4, x5, s0);
        const bool verbose = g_logged.fetch_add(1) < 32;
        const int kart = rd<std::int32_t>(v, kPlayerIdx);

        // ---- stat cap: lower the body row by each column's excess --------
        std::int32_t* body = nullptr;
        int delta[rules::kStatCols] = {};
        int n = 0;
        if (cap < rules::kStatMax) {
            const u64 tbl = statTable();
            const int ids[4] = {rd<std::int32_t>(v, kBodyId), driverRowIndex(v), rd<std::int32_t>(v, kTireId),
                                rd<std::int32_t>(v, kWingId)};
            std::int32_t* rows[4] = {};
            bool ok = tbl != 0;
            for (int k = 0; ok && k < 4; ++k) ok = (rows[k] = rowFor(tbl, k, ids[k])) != nullptr;
            if (!ok) {
                if (verbose)
                    MK8AP_LOG_WARN("[statcap] stat tables not readable (tbl=0x%llx); stats left vanilla",
                                   static_cast<unsigned long long>(tbl));
            } else {
                logLevelTablesOnce(tbl);
                int sums[rules::kStatCols];
                for (int c = 0; c < rules::kStatCols; ++c)
                    sums[c] = rows[0][c] + rows[1][c] + rows[2][c] + rows[3][c];
                n = rules::statCapDeltas(sums, cap, delta);
                if (verbose)
                    MK8AP_LOG_INFO("[statcap] kart %d parts d%d b%d t%d g%d (driver row %d) cap %d: "
                                   "sums %d %d %d %d %d %d %d %d %d %d %d %d %d %d, %d column(s) lowered",
                                   kart, rd<std::int32_t>(v, kDriverId), ids[0], ids[2], ids[3], ids[1], cap,
                                   sums[0], sums[1], sums[2], sums[3], sums[4], sums[5], sums[6], sums[7],
                                   sums[8], sums[9], sums[10], sums[11], sums[12], sums[13], n);
                if (n > 0) body = rows[0];
            }
        }

        std::int32_t saved[rules::kStatCols];
        if (body) {
            for (int c = 0; c < rules::kStatCols; ++c) {
                saved[c] = body[c];
                body[c] -= delta[c];
            }
        }
        const u64 ret = g_setupStat.orig(v, x1, x2, x3, x4, x5, s0);
        if (body) {
            for (int c = 0; c < rules::kStatCols; ++c) body[c] = saved[c];
        }

        // ---- base top speed ---------------------------------------------
        const bool scaled = pct < 100 && scaleBaseSpeed(v, pct);
        if (pct < 100 && !scaled && verbose)
            MK8AP_LOG_WARN("[statcap] kart %d base speeds not as expected; top speed left vanilla", kart);
        if (verbose) {
            const u64 move = rd<u64>(v, kMove);
            const auto* got = reinterpret_cast<const std::int32_t*>(v + 0x2DC);
            MK8AP_LOG_INFO("[statcap] kart %d game sums %d %d %d %d %d %d %d %d %d %d %d %d %d %d; "
                           "base speed %d%% -> G %.3f/%.3f air %.3f",
                           kart, got[0], got[1], got[2], got[3], got[4], got[5], got[6], got[7], got[8],
                           got[9], got[10], got[11], got[12], got[13], scaled ? pct : 100,
                           looksLikePtr(move) ? rd<float>(move, 0x4B8) : -1.0f,
                           looksLikePtr(move) ? rd<float>(move, 0x4BC) : -1.0f,
                           looksLikePtr(move) ? rd<float>(move, 0x50C) : -1.0f);
        }
        return ret;
    });

}  // namespace

void installStatCapHook() {
    const auto off = sym::mainOffset(kSymbol);
    const auto abs = sym::addr(kSymbol);
    if (!off || !abs) { MK8AP_LOG_WARN("[statcap] %s unresolved; stat cap off", kSymbol); return; }
    g_mainBase = abs - off;
    // Sanity: the Mii map must hold three valid driver rows ({17, 0, 10} on 4.0.1).
    for (int i = 0; i < 3; ++i) {
        const int d = rd<std::int32_t>(g_mainBase, kMiiDriverMap + 4 * i);
        if (d < 0 || d > 64) {
            MK8AP_LOG_WARN("[statcap] Mii driver map looks wrong (%d at %d); stat cap off", d, i);
            return;
        }
    }
    const auto rc = g_setupStat.installAtMainOffset(off);
    MK8AP_LOG_INFO("[statcap] %s @+0x%llx: %s", kSymbol, static_cast<unsigned long long>(off),
                   rc.succeeded() ? "installed" : "FAILED");
}

}  // namespace mk8ap::hooks
