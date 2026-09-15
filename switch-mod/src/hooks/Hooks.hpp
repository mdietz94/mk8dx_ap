// One install function per hook target. Each checks that its symbol
// resolved (sym::addr != 0) and logs + skips otherwise, so a version drift
// degrades one feature at a time instead of aborting the module.

#pragma once

namespace mk8ap::hooks {

// SDK-level hooks (resolved by name through nnrtld; no game symbols needed)
void installSocketInitHook();   // nn::socket::Initialize observer
void installPlayReportHooks();  // nn::prepo IPC SaveReport capture (race results)

// Game hooks (pattern / 3.0.5-offset resolved)
void installSaveLoadHook();     // gear::SaveDataFile::load -> start networking
void installRaceFinishHook();   // ui::RaceWindow::onFakeGoal -> race_result
void installCupGateHooks();     // FlagAccessor::isOpen / DLCManager::getCupStatus / Control_CupButton::setCup
void installItemGateHook();     // gear::ItemOwner::startSlot
void installKartGuardHook();    // ui::SetNextCourse (enforce driver/parts, learn course)
void installCourseTableHook();  // ui::Control_CourseButton::setCourse (learn course ids)
void installSpeedCapHook();     // ui::Control_RaceLapCoin::onCalc_ (per-frame clamp)

// SocketInitHook helpers used by ApClient::start.
bool socketInitialized();
bool ensureSocketInitialized();

// IdTableDump: enumerate mush::E*ID::text_ and ship the tables once.
void dumpIdTablesOnce();

}  // namespace mk8ap::hooks
