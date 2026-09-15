// Offset-based accessors for the handful of game structures we read or
// write. Offsets come from fishguy6564/MK8DX-Headers (64-bit branch) and
// the RoGamer97 3.0.5 patch comments; each one is annotated with its source
// so a future version bump knows what to re-verify.
//
// Deliberately NOT full struct mirrors: a wrong padding byte in a mirrored
// 0x600-byte class silently shifts every later field, whereas a named
// offset is either right or obviously wrong in the logs.

#pragma once

#include <cstddef>
#include <cstdint>

namespace mk8ap::game {

template <typename T>
inline T rd(const void* base, std::size_t off) {
    return *reinterpret_cast<const T*>(static_cast<const std::uint8_t*>(base) + off);
}
template <typename T>
inline void wr(void* base, std::size_t off, T v) {
    *reinterpret_cast<T*>(static_cast<std::uint8_t*>(base) + off) = v;
}

// ---- gear::RaceInfo (MK8DX-Headers gear/Race/RaceInfo.hpp) ----------------
namespace race_info {
inline constexpr std::size_t kRule        = 0x08;   // ERaceRule (s32)
inline constexpr std::size_t kBattleType  = 0x0C;
inline constexpr std::size_t kEngineClass = 0x10;   // EEngineLevelSlot (s32)
inline constexpr std::size_t kIsMirror    = 0x26;   // bool
inline constexpr std::size_t kKartInfos   = 0x30;   // RaceKartInfo[12]
inline constexpr std::size_t kPlayerAmount = 0x180; // u32
inline constexpr std::size_t kCourseId    = 0x1A0;  // u32 (mush::ECourseID)
inline constexpr int kMaxKarts = 12;
}  // namespace race_info

// ---- gear::RaceKartInfo (0x1C bytes) ---------------------------------------
namespace kart_info {
inline constexpr std::size_t kSize     = 0x1C;
inline constexpr std::size_t kBodyId   = 0x00;
inline constexpr std::size_t kTireId   = 0x04;
inline constexpr std::size_t kGliderId = 0x08;
inline constexpr std::size_t kDriverId = 0x0C;
inline constexpr std::size_t kDriverVariant = 0x10;  // u8
inline constexpr std::size_t kPlayerType = 0x14;     // s32 (gear::RacePlayerType)
inline constexpr std::size_t kTeam = 0x18;
}  // namespace kart_info

inline void* kartInfoAt(void* raceInfo, int idx) {
    return static_cast<std::uint8_t*>(raceInfo) + race_info::kKartInfos
           + static_cast<std::size_t>(idx) * kart_info::kSize;
}

// ---- ui::CourseInfo (MK8DX-Headers ui/Course/CourseInfo.hpp) --------------
namespace course_info {
inline constexpr std::size_t kCourseId   = 0x00;  // mush::ECourseID (s32)
inline constexpr std::size_t kPrefixMsbt = 0x04;
inline constexpr std::size_t kCourseMsbt = 0x08;
inline constexpr std::size_t kIsDLC      = 0x0C;  // bool
// sead::FixedSafeString<64> mCourseIconResource follows at 0x10:
//   +0x00 vtable, +0x08 const char* mStringTop, +0x10 s32 mBufferSize,
//   then the inline 64-byte buffer. We read through mStringTop so the exact
//   inline layout doesn't matter.
inline constexpr std::size_t kIconResource = 0x10;
inline constexpr std::size_t kSafeStringTop = 0x08;
}  // namespace course_info

inline const char* courseInfoIconName(const void* ci) {
    if (!ci) return nullptr;
    const auto* sstr = static_cast<const std::uint8_t*>(ci) + course_info::kIconResource;
    return rd<const char*>(sstr, course_info::kSafeStringTop);
}

// ---- Scene -> kart chain (RoGamer97 FINISH patch + MK8DX-Examples) -------
//   gear::FrameworkUtil::getCurrentGameScene()   -> FrameworkGameScene*
//   scene + 0x1B0                                -> object::ObjectEngine*
//   engine + 0x218                               -> object::RaceDirector*
//   engine + 0x238                               -> object::KartDirector*
//   kartDirector + 0xC8                          -> KartUnitHolder** (array)
//   holder[i] + 0x08                             -> object::KartVehicle*
//   vehicle + 0x28                               -> object::KartVehicleMove*
//   raceDirector + 0x68                          -> gear::RaceKartChecker** (array)
//   checker + 0x40                               -> s32 current rank (0-based)
namespace scene {
inline constexpr std::size_t kObjectEngine = 0x1B0;
inline constexpr std::size_t kRaceDirector = 0x218;
inline constexpr std::size_t kKartDirector = 0x238;
inline constexpr std::size_t kKartUnitHolders = 0xC8;
inline constexpr std::size_t kHolderKartVehicle = 0x08;
inline constexpr std::size_t kVehicleMove = 0x28;
inline constexpr std::size_t kRaceKartCheckers = 0x68;
inline constexpr std::size_t kCheckerRank = 0x40;
}  // namespace scene

// ---- object::KartVehicle (MK8DX-Headers object/Kart/KartVehicle.hpp) ------
namespace vehicle {
inline constexpr std::size_t kPlayerId = 0xA8;
inline constexpr std::size_t kBodyId   = 0xB0;
inline constexpr std::size_t kDriverId = 0xB4;
inline constexpr std::size_t kIsMaster = 0xD0;  // bool: locally controlled
inline constexpr std::size_t kIsCpu    = 0xD2;
inline constexpr std::size_t kIsNetSend = 0xE6;
}  // namespace vehicle

// ---- object::KartVehicleMove (header warns: "still based off of 32-bit
// structure" - SpeedCapHook logs the raw values once so drift is visible) --
namespace vehicle_move {
inline constexpr std::size_t kSpeed     = 0x37C;  // float, current speed
inline constexpr std::size_t kTopSpeed1 = 0x424;  // float
inline constexpr std::size_t kTopSpeed2 = 0x428;
inline constexpr std::size_t kTopSpeed3 = 0x42C;
}  // namespace vehicle_move

// ---- gear::ItemOwner (MK8DX-Headers gear/Item/ItemOwner.hpp) -------------
namespace item_owner {
inline constexpr std::size_t kPlayerId = 0x40;
inline constexpr std::size_t kKartInfoProxy = 0x48;   // object::KartInfoProxy*
inline constexpr std::size_t kProxyKartVehicle = 0x48; // proxy + 0x48 -> KartVehicle* (Debug Item Cycler)
}  // namespace item_owner

// ---- gear::SaveData ---------------------------------------------------------
namespace save_data {
inline constexpr std::size_t kFlagAccessor = 0x190;
}

}  // namespace mk8ap::game
