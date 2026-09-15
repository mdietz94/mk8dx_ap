// Thin, null-safe wrappers over the resolved game functions + the pointer
// chains in Structs.hpp. Every accessor returns nullptr / -1 when a symbol
// is unresolved or a pointer in the chain is null, so hooks can early-out
// instead of faulting on a version drift.

#pragma once

#include <cstdint>

namespace mk8ap::game {

// Bind function pointers from sym::PatternResolver. Call once after
// sym::resolveAll(). Safe to call again (idempotent).
void bindGameApi();

void* getRaceInfo();       // gear::GetRaceInfo()
void* getNextRaceInfo();   // gear::GetNextRaceInfo()
void* getCurrentGameScene();
int   getMyKartIndex();    // -1 offline -> callers clamp to 0
void* getCurrentUserSaveData();

// Local player kart index (getMyKartIndex clamped to 0).
int localKartIndex();

// KartVehicle* / KartVehicleMove* for kart `idx` via the scene chain.
void* kartVehicle(int idx);
void* kartVehicleMove(int idx);
// Current rank (0-based) for kart `idx` via RaceDirector -> RaceKartChecker.
int   kartRank(int idx);

// mush::E*ID::text_(int) -> internal identifier string, or nullptr.
const char* driverText(int id);
const char* bodyText(int id);
const char* tireText(int id);
const char* wingText(int id);

// Convenience reads on the current RaceInfo (or defaults when null).
int  currentRule();        // ERaceRule or -1
int  currentEngineClass(); // EEngineLevelSlot or -1
int  currentCourseId();    // or -1
bool currentIsMirror();

}  // namespace mk8ap::game
