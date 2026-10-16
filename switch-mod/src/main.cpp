// MK8DX Archipelago subsdk entry point (LibHakkun).
//
// hkMain runs at module load, before the game's own init:
//   1. check the build id is 4.0.0 and resolve game symbols (sym::resolveAll);
//      on any other build the mod stays inactive
//   2. bind the game API function pointers
//   3. install SDK hooks (socket init observer, PlayReport capture)
//   4. install game hooks (each skips itself if its symbol is unresolved)
// Networking starts later, from the first gear::SaveDataFile::load.

#include "hk/types.h"

#include "ap/ApState.hpp"
#include "game/GameApi.hpp"
#include "hooks/Hooks.hpp"
#include "sym/PatternResolver.hpp"
#include "util/Log.hpp"

extern "C" void hkMain() {
    MK8AP_LOG_INFO("=== mk8dx_archipelago %s hkMain START (target MK8DX %s) ===",
                   MK8AP_MOD_VERSION_STRING, MK8DX_VERSION_STRING);

    (void)mk8ap::ap::ApState::instance();

    mk8ap::sym::resolveAll();
    if (!mk8ap::sym::isSupportedBuild()) {
        MK8AP_LOG_ERROR("=== hkMain END: unsupported game build, mod inactive "
                        "(update Mario Kart 8 Deluxe to 4.0.0) ===");
        return;
    }
    mk8ap::game::bindGameApi();

    MK8AP_LOG_INFO("installing SDK hooks");
    mk8ap::hooks::installSocketInitHook();
    mk8ap::hooks::installPlayReportHooks();
    mk8ap::hooks::dumpSdkSymbolsOnce();

    MK8AP_LOG_INFO("installing game hooks");
    mk8ap::hooks::installSaveLoadHook();
    mk8ap::hooks::installRaceFinishHook();
    mk8ap::hooks::installCupGateHooks();
    mk8ap::hooks::installItemGateHook();
    mk8ap::hooks::installKartGuardHook();
    mk8ap::hooks::installSpeedCapHook();

    MK8AP_LOG_INFO("=== hkMain END (symbols %d/%d; networking starts on save load) ===",
                   mk8ap::sym::resolvedCount(), mk8ap::sym::totalCount());
}
