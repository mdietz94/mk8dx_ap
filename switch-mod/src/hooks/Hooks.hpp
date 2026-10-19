// One install function per hook target. Each checks that its symbol
// resolved (sym::addr != 0) and logs + skips otherwise, so a version drift
// degrades one feature at a time instead of aborting the module.

#pragma once

namespace mk8ap::hooks {

// SDK-level hooks (resolved by name through nnrtld; no game symbols needed)
void installSocketInitHook();   // nn::socket::Initialize observer
void installPlayReportHooks();
void installRomConfigHook();    // nn::fs::MountRom -> read ap_config.json (discovery seed)  // nn::prepo IPC SaveReport capture (race results)

// Game hooks (resolved by sym::PatternResolver)
void installSaveLoadHook();     // gear::SaveDataFile::load -> start networking
void installRaceFinishHook();   // ui::RaceWindow::onFakeGoal -> race_result
void installCupGateHooks();     // FlagAccessor::isOpen / DLCManager::getCupStatus / Control_CupButton::setCup
void installItemGateHook();     // gear::ItemOwner::startSlot
void installKartGuardHook();    // ui::SetNextCourse (enforce driver/parts)
void installSpeedCapHook();     // ui::Control_RaceLapCoin::onCalc_ (per-frame clamp)

// Start the AP worker + dump the id tables, once. Called from the save-load
// hook and, in case that pattern did not resolve, from every other hook that
// fires once the game is up (menus, races, PlayReport). `trigger` is logged.
void startNetworkingOnce(const char* trigger);

// SocketInitHook helpers used by ApClient::start.
bool socketInitialized();
bool ensureSocketInitialized();

// IdTableDump: enumerate mush::E*ID::text_ and ship the tables once.
void dumpIdTablesOnce();

// SdkSymbolDump: log nnSdk dynsym names matching IPrepoService/SaveReport
// so the SDK 4.4.0 cmif proxy mangled names can be read off the boot log.
void dumpSdkSymbolsOnce();

}  // namespace mk8ap::hooks
