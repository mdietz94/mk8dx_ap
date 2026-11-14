// Grand Prix result: the game's own "a cup is over, record the trophy"
// function (4.0.1 +0x552FB0, unnamed in public sources; name is ours). It
// is reached only from the GP results flow (+0x543E28 at +0x543EF4, and the
// +0x552CD8 tail of +0x55211C) and runs whatever the save already holds, so
// it reports a gold on a 100% save too.
//
// Signature (4.0.1 disassembly): void(GpResultArg* p, bool* saveDirty)
//   p[0]   s32  must be 0 (the function returns at once otherwise)
//   p[1]   s32  cup (mush::EGrandPrixID, 0-based)
//   p[2]   s32  engine slot (save rows are p[2] * 0x20 + p[1])
//   p[4 + 2i] s32 player i kind; only kinds 0 / 2 earn a trophy
//   p[0xC] u32  local player count
// For local player i it takes Player = PlayerManager->ptrs[PlayerManager+0x17C+4i]
// and the head of the Player's GP history ring (Player+0x58 buf, +0x60 cap,
// +0x64 head, +0x68 count; 5 x u32 per entry):
//   entry[0] = total GP points (>= 60 three stars, 57..59 two, 54..56 one)
//   entry[2] = final GP placing, 1 = first (1/2/3 -> gold/silver/bronze)
// and writes max(old, trophy) to save+0x688 + engine*0x20 + cup. We read the
// same entry before calling the original and report it, for every placing;
// the PC credits "Complete <cup> (Grand Prix)" for any placing and
// "Win <cup> (Grand Prix)" only for placing 1.

#include "Hooks.hpp"

#include <cstdint>

#include "hk/hook/Trampoline.h"

#include "../ap/ApFrameBridge.hpp"
#include "../game/GameApi.hpp"
#include "../game/Structs.hpp"
#include "../sym/PatternResolver.hpp"
#include "../util/Log.hpp"

namespace mk8ap::hooks {

namespace {

using u64 = std::uint64_t;

constexpr std::size_t kLocalPlayerIdx = 0x17C;  // u32[] after the local count (+0x178)
constexpr std::size_t kHistBuf = 0x58, kHistCap = 0x60, kHistHead = 0x64, kHistCount = 0x68;

bool looksLikePtr(u64 p) { return p >= 0x10000 && (p & 0x3) == 0; }

// Head entry of local player `i`'s GP history, or nullptr.
const std::uint32_t* historyHead(int i) {
    void* pm = game::getPlayerManager();
    if (!pm || i < 0 || i > 3) return nullptr;
    const std::uint32_t idx = game::rd<std::uint32_t>(pm, kLocalPlayerIdx + 4 * i);
    const std::uint32_t num = game::rd<std::uint32_t>(pm, game::player_manager::kPlayersNum);
    if (idx == 0xFFFFFFFFu || idx >= num) return nullptr;
    const u64 ptrs = game::rd<u64>(pm, game::player_manager::kPlayersPtrs);
    if (!looksLikePtr(ptrs)) return nullptr;
    const u64 player = reinterpret_cast<const u64*>(ptrs)[idx];
    if (!looksLikePtr(player)) return nullptr;
    const auto* pl = reinterpret_cast<const void*>(player);
    const u64 buf = game::rd<u64>(pl, kHistBuf);
    if (!looksLikePtr(buf)) return nullptr;
    const std::int32_t count = game::rd<std::int32_t>(pl, kHistCount);
    if (count == 0) return reinterpret_cast<const std::uint32_t*>(buf);
    const std::int32_t cap = game::rd<std::int32_t>(pl, kHistCap);
    const std::int32_t head = game::rd<std::int32_t>(pl, kHistHead);
    const std::int32_t slot = head - (cap <= head ? cap : 0);
    if (slot < 0 || cap <= 0 || slot >= cap) return nullptr;
    return reinterpret_cast<const std::uint32_t*>(buf) + 5 * slot;
}

HkTrampoline<u64, u64, u64> g_gpResult = hk::hook::trampoline([](u64 p, u64 dirty) -> u64 {
    startNetworkingOnce("gp_result");
    if (looksLikePtr(p)) {
        const auto* a = reinterpret_cast<const std::int32_t*>(p);
        const int gate = a[0], cup = a[1], engine = a[2];
        const std::uint32_t players = static_cast<std::uint32_t>(a[0xC]);
        const std::uint32_t* e = players > 0 ? historyHead(0) : nullptr;
        const int kind = a[4];
        if (e) {
            MK8AP_LOG_INFO("[gp] result cup=%d engine=%d gate=%d players=%u kind0=%d: placing=%u points=%u "
                           "(entry %u %u %u %u %u)",
                           cup, engine, gate, players, kind, e[2], e[0], e[0], e[1], e[2], e[3], e[4]);
            if (gate == 0 && (kind | 2) == 2 && cup >= 0 && cup < 48)
                ap::reportGpResult(cup, engine, static_cast<int>(e[2]), static_cast<int>(e[0]));
        } else {
            MK8AP_LOG_WARN("[gp] result cup=%d engine=%d gate=%d players=%u: player 0 history not readable",
                           cup, engine, gate, players);
        }
    }
    return g_gpResult.orig(p, dirty);
});

}  // namespace

void installGpResultHook() {
    const auto off = sym::mainOffset("ui::GrandPrix::commitResult");
    if (!off) {
        MK8AP_LOG_WARN("[gp] GP result writer unresolved; cup checks will not fire");
        return;
    }
    const auto rc = g_gpResult.installAtMainOffset(off);
    MK8AP_LOG_INFO("[gp] hook @+0x%llx: %s", static_cast<unsigned long long>(off),
                   rc.succeeded() ? "installed" : "FAILED");
}

}  // namespace mk8ap::hooks
