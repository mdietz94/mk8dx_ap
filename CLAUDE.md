# CLAUDE.md - MK8DX Archipelago

Fast-load brief for the **Mario Kart 8 Deluxe** Archipelago project. Sibling
of `smo_archipelago` and `smbw_ap` (same two-tier shape: LibHakkun subsdk9
on the Switch, Kivy client inside the apworld on the PC). Everything in
"Verified on the real game" below was observed on a real 4.0.0 dump under
Ryujinx 1.3.3 on 2026-10-16 (boot / symbols / hooks / handshake re-checked
on 4.0.1 on 2026-10-21); everything in "Not yet verified" has not been.

| Identifier | Value |
|---|---|
| AP game name | `Mario Kart 8 Deluxe` |
| apworld zip | `mk8dx.apworld` (imports as `worlds.mk8dx`; host.yaml key `mk8dx_options`) |
| In-repo folder | `apworld/mk8dx_archipelago/` |
| Title id | `0100152000022000` |
| Target version | **4.0.1 only** (main build id `158C0C7399AABD7BF83795C2EF3484C7`). User decision 2026-10-21: support only the latest update, no 4.0.0 compatibility (4.0.0 = `2C336A9BCF79C3040CE506CDD391B578` was the target until then; 3.0.5 never). The mod stays inactive on any other build. |
| Ports | TCP 17778 (bridge), UDP 17779 (discovery). SMO uses 17777/17776. |

## Never commit Nintendo IP

No NSO/NSP/XCI dumps, no `.text` dumps (`make_pattern.py` input), no
extracted romfs, no keys. Byte patterns of a few dozen bytes and function
addresses are fine (they are the same data every public patch carries).
Course / character names are the community-standard names already used
by the MK8DX Manual world and the MKWii apworld. The user's game dump lives
outside the repo (`Desktop\ROMs\Switch\Mario Kart 8 Deluxe\`, XCI base +
v1507328 = 4.0.1 update NSP; Ryujinx's selected update is in
`%APPDATA%/Ryujinx/games/0100152000022000/updates.json`) and must stay there.

## Status (2026-10-21)

Alpha, and the first version that has been played against a live seed.
Thirteen commits; `git log` is the change narrative, `docs/symbols.md` the
on-game findings. Verified in game: boot, 20/21 symbols, race results
(both channels), AP checks and item receipt, item gate, loadout gate, cup
gate, speed clamp engaging. Not verified: Grand Prix completion (Trophy)
and goal, and anything on real Switch hardware (Ryujinx only so far).

4.0.1 (2026-10-21): Ro's 4.0.1 sources are the 4.0.0 ones with addresses
moved (one unused GOT slot aside), so only the six `address` entries
changed. Verified on 4.0.1 in Ryujinx: build id match, 20/21 symbols (each
pattern hit moved by the same per-region delta as Ro's addresses), every
hook installed, ap_config.json read, client handshake + state, the
title-screen demo race ran the KartDirector / SetRandomCPU hooks without a
crash. Races, gates and checks have not been re-played on 4.0.1.

Installer (2026-10-18): the release `mk8dx.apworld` bundles a PREBUILT mod
(`_setup/mod/`, SHA-256 manifest); the Kivy wizard (`/setup`, auto-opens on
the client's first run) deploys it to Ryujinx / SD card / folder. Verified
from the frozen AP 0.6.7 install in `C:\ProgramData\Archipelago` (wizard,
Ryujinx deploy, Kart tab) and in game: `MountRom(content)` ->
`content:/ap_config.json: bridge_host=...` -> sweep finds the client. The Ubuntu
release workflow has not run yet (first run is the PR that adds it).

## How to test (the loop that produced every finding so far)

```pwsh
# 1. build + deploy the mod (deploy also writes romfs/ap_config.json with this PC's LAN IP)
python scripts\build_switchmod.py
python scripts\deploy_mod.py ryujinx      # the wizard's deploy code, from switch-mod/build/exefs
# 2. (re)install the apworld, then host a seed and run the headless client
.venv\Scripts\python scripts\install_apworld.py
.venv\Scripts\python scripts\ap_generate.py --player_files_path <abs dir with yaml> --outputpath <abs out dir>
.venv\Scripts\python scripts\ap_server.py --port 38281 --host 127.0.0.1 <out>\AP_*.archipelago
.venv\Scripts\python scripts\run_client.py --nogui --connect localhost:38281 --name Mario
# 3. launch the game and read the newest Ryujinx log
& "C:\Users\maxwe\Desktop\ryujinx-1.3.3\Ryujinx.exe" "<path to the .xci>"
```

Read `Desktop\ryujinx-1.3.3\Logs\<newest>.log`: grep `InvalidAccessHandler`
first (a crash prints registers + a guest stack trace; `X[n]` values that
are stack addresses truncated to 32 bits mean a hook declared a pointer
argument as `int`), then the `[mk8ap ...]` lines. The client's stdout
mirrors the Switch log (`[switch:info]` lines) and adds its own
(`race_result[hook]`, `play_report race:`, `checked:`, `received`).
Ryujinx also prints every PlayReport as JSON (`ServicePrepo
ProcessPlayReport`): a free cross-check for the mod's MessagePack capture.
The `ap_generate` output is a zip; unzip it to get the `.archipelago`.
`run_client.py` prints harmless `Could not load world` errors for other
worlds' missing deps. Generate seeds with `course_selection:
base_game` unless the save owns the BCP (the user's does not).

## Load-bearing design points

* **One build, checked at boot.** `sym::resolveAll()` reads main's build
  id; if it is not 4.0.1, `hkMain` returns before installing anything.
* **No symbols, no sail.** `switch-mod/syms/mk8dx_symbols.toml` ->
  `scripts/gen_symbol_table.py` -> `src/sym/SymbolTable.inc` (committed;
  re-run the script after editing the toml). Entries have a published
  4.0.1 `address` (RoGamer97's 4.0.1 patch sources) or a `pattern`
  (fishguy6564/MK8DX-Base-Plugin `symbols.toml`, authored on 3.0.5, 1739
  lines, many more functions than we use: fetch it when a new hook target
  is needed). The resolver tries exact bytes, then a relaxed form (branch
  distances and struct-offset immediates wildcarded) that must match
  exactly once. 20/21 resolve on 4.0.1 (and did on 4.0.0); only `DLCManager::getCupStatus`
  does not (relaxed form not unique). Addresses: `docs/symbols.md`.
* **Hooks are function-entry trampolines** (`HkTrampoline`,
  `installAtMainOffset`). 4.0.0's register reshuffles inside functions do
  not matter here. Do not add mid-function hooks.
* **Game "enums" travel by const reference.** `mush::E*ID`, `ui::ECup`,
  `ui::DLCState`, `gear::EItemSlot` are class wrappers with user-declared
  destructors: a hooked function receives a POINTER to a 32-bit value in
  x1/x2, and a function RETURNING one gets a hidden x8 result pointer.
  Rules: declare pattern-located hooks' integer registers as `u64` and
  forward x1..x5 untouched (`derefEnum()` reads the value only when the
  register looks like a pointer); override an argument by pointing
  `orig()` at a local copy; for an sret return use the asm stubs in
  `CupGateHook.cpp` (`mk8ap_isDlcEntry` hands x8 to C++, `mk8ap_callSret`
  reloads it for the original). Declaring these as `int` truncated the
  pointers and crashed twice (Prepare Thread at the cup grid, MainThread
  at the first item roll).
* **Enum numbering.** `ui::ECup` is 0-based (Mushroom = 0 ... Spiny = 23)
  and equal to `mush::EGrandPrixID`; the header's "Mushroom = 1" is wrong
  for 4.0.0. Driver/body/tire/glider ids in `client/tables.py` are the
  verified 4.0.0 values (checked one-for-one against RoGamer97's "CPU Combo
  Modifier" enum lists; Link's Champion's Tunic form is driver 43, the BCP
  Mii is 52, Birdo..Pauline are 44..51). The runtime `mush::E*ID::text_`
  tables are indexed enum+1 (index 0 = "Invalid") and use short codes, so
  the client logs them but does NOT learn part ids from them. Course ids
  ARE learned: `race_result` (numeric id) paired with the `play_report`
  (course string) of the same race, persisted by `client/learned.py` to
  `mk8dx_learned_ids.json` in the AP user dir. Known: course 27 =
  `Gu_FirstCircuit` (Mario Kart Stadium).
* **Race results come from the game's own PlayReport** (nn::prepo IPC
  proxy hook, MessagePack decoded in `client/play_report.py`). MK8DX ships
  NintendoSDK 4.4.0, whose cmif proxy names differ from smbw_ap's SDK 15+
  ones (`CmifDomainProxy`, `MessageType 4`, no `ProcessModifierImpl`
  suffix); `SdkSymbolDump.cpp` logs matching dynsym names at boot: use it
  before guessing any SDK mangled name. Never hook `PlayReport::Add/Save`
  (delayed SDK abort; see smbw_ap). `ui::RaceWindow::onFakeGoal` is the
  fallback channel: it fires once per RaceWindow, i.e. for EVERY kart
  auto-finished at race end, so the hook keeps only the kart whose
  `KartVehicle+0xD0` (isMaster) is set, and the client accepts one hook
  result per course per 30 s. Ranks there are 0-based; `RaceWindow+0xF4`
  is NOT the kart index on 4.0.0 (the window-id derivation is used).
* **Loadout source of truth is `gear::Player::mKartInfo`** (`Player+0x210`,
  reached via `GetPlayerManager()` -> `+0x50` PtrArray of 16 slots,
  `+0x178` local player count). The UI copies Player -> RaceInfo when the
  race is assembled and builds the karts from that copy. Rewriting
  `GetNextRaceInfo()`/`GetRaceInfo()` kart infos at `ui::SetNextCourse`,
  after `ui::SetRandomCPU`, or pre-orig in
  `object::KartDirector::prepareBeforeStructure` never changed the race
  (three races, PlayReport kept the menu pick). Rewriting the Player's
  kart info works. `KartGuardHook::enforceLoadouts()` does both; keep the
  RaceInfo rewrites as belt-and-braces only.
* **Cup gate lives in `static DLCState Page_CourseBase::isDLC(ECup)`,
  returning `NotSale(0)`** (0.2.1). Do NOT return `Locked(4)` (0.2.0): the
  grid setup then clears the button's enabled flag (`Control+0x3CB`), the
  cursor cannot land on it, and an open cup boxed in by locked ones
  (Mushroom + Lightning) is unreachable (player report 2026-10-25). With
  `NotSale` the cursor moves freely, the preview shows "???" courses, and
  the decide handler (which re-reads isDLC at press time) plays only the
  deny window. `NotBuy(2)` would reach the eShop purchase path. Derived
  from the 4.0.1 disassembly (CupGateHook.cpp has the call-site map);
  not yet replayed in game. Do NOT gate via `FlagAccessor::isOpen`
  returning false (isDLC turns a closed base cup into `Locked`, which
  disables every button: soft-lock; the hook now only logs would-be
  denials). `Control_CupButton::setCup`'s DLCState only picks icon frames
  (0 and 4 draw alike), so a gated cup does not look padlocked in the grid.
* **Item gate**: `gear::ItemOwner::startSlot(this, int slot, const
  EItemSlot&, bool, bool)`; locked rolls become Coin (or None). Verified.
* **Speed**: `ui::Control_RaceLapCoin::onCalc_` clamps
  `KartVehicleMove+0x37C` to `top1 * speed_pct` every frame; top-speed
  floats at `+0x424/+0x428/+0x42C` read sane on 4.0.0. **Engine class is
  deliberately NOT restricted** (user decision 2026-10-16): the speed
  restriction is the clamp only, scaled by Progressive Speed.
  `engine_max` in the state message is informational.
* **State is a full snapshot** (`state` message), re-sent on every change.
  No item replay, nothing to double-count. The client sends
  `enabled=false` until an AP slot is connected and again on disconnect,
  so a client with no server leaves the game vanilla (before this fix a
  bare client collapsed the game to the starting unlocks).
* **Networking starts once the game is up,** from `gear::SaveDataFile::load`
  (fires twice at boot) or, failing that, from the first menu / race /
  PlayReport hook (`hooks::startNetworkingOnce`). Never from `hkMain`.
  `SocketInitHook` initialises `nn::socket` itself only if the game has
  not; discovery is loopback first, then a /24 unicast sweep.
* **Discovery = the dread_ap scheme (user decision 2026-10-19).** Deploy
  writes `<title>/romfs/ap_config.json` = `{"bridge_host":"<PC LAN IP>"}`
  (detected, editable in the wizard / `--bridge-host`). `RomConfigHook`
  trampolines `nn::fs::MountRom` (MK8DX mounts `content`) and reads
  `content:/ap_config.json` ONCE, on that game thread, closing every handle
  (Dread hit a save-breaking 2002-6455 FileNotClosed abort reading on a
  worker thread). `ApDiscovery` probes loopback, then sweeps the seed's /24;
  it never calls nn::fs and never asks the console for its own address
  (nifm / getsockname are unproven on real hardware; Dread's seed is
  proven). No compile-time `BRIDGE_HOST` any more. The nn::fs read functions
  are `lookupSymbol`'d (4.0.0 names verified via SdkSymbolDump).
* **Release = prebuilt mod in the apworld.** `scripts/build_release.py`
  (CI: `.github/workflows/release.yml`, Ubuntu + LLVM 19) builds with
  `-DMK8AP_MOD_VERSION=<world_version>`, bundles via
  `install_apworld.py --bundle-mod`, runs `scripts/release_audit.py`
  (allowlist, no dumps/keys/assets, manifest integrity, LZ4-decompresses the
  NSO and rejects any IPv4 literal but 127.0.0.1 and any home path, version
  drift between manifest / archipelago.json / client `__version__`), writes
  the `.sha256`. Bump `world_version` and `__version__` together.
* **Setup package `_setup/`** imports neither Archipelago nor Kivy except in
  `wizard.py`; tests (`tests/test_setup.py`) import it as top-level `_setup`.
  State lives in `%APPDATA%\MK8DXArchipelago\` (`MK8AP_APPDATA_ROOT`
  overrides it for tests and sandboxed runs). No `.mk8dxap` file: the mod
  needs nothing per seed (SMO's generated patch file confused archipelago.gg).
* **`ui::CourseInfo` has no name string.** An earlier header revision
  listed one; reading it would dereference garbage.

## Not yet verified

* Anything past boot on 4.0.1 (races, gates, checks: see Status).
* The 0.2.1 `NotSale` cup gate: cursor crossing locked cups, the deny
  window on A, a cup received while the grid is open.
* Completing a Grand Prix -> `Complete <cup> (Grand Prix)` check -> Trophy,
  and the goal (`goal_cups` Trophies).
* Real Switch hardware (Atmosphere): only Ryujinx so far. Discovery sweep,
  nifm request path, the socket pool and the LayeredFS ap_config.json read
  have not been exercised on-console.
* `restrict_cpu_items`, multiplayer (local players > 1), VS / Time Trials
  flows beyond one GP race, Booster Course Pass cups (user's save lacks it).
* `KartGuard` with a non-default `def_*` (so far only Mario / Standard).

## Layout

```
apworld/mk8dx_archipelago/   __init__.py (MK8DXWorld + Launcher Component + run_setup_wizard), Data/Items/Locations/
                             Regions/Rules/Options/Helpers/Meta/DataValidation.py (Manual template),
                             hooks/{Options,World,Rules,...}.py, data/*.json (GENERATED by
                             scripts/gen_apworld_data.py from client/tables.py: 180 items, 313 locations),
                             client/{main,context,gui,switch_server,discovery,protocol,play_report,
                                     learned,resolver,state,tables,net_util,logging_setup}.py,
                             _setup/{bundle,deploy,state,firewall,cli,wizard}.py (mod/ only in release zips),
                             tests/ (61 pass + 7 skipped: live-AP ones need MK8AP_LIVE_AP=1, lz4 test needs lz4)
switch-mod/                  CMakeLists.txt, config/, syms/mk8dx_symbols.toml, sys/ (LibHakkun submodule,
                             Windows patches applied at build by scripts/patch_hakkun.py),
                             src/main.cpp, src/sym/{PatternResolver,SymbolTable.inc},
                             src/ap/{ApClient,ApState,ApProtocol,ApFrameBridge,ApDiscovery,ApConfig},
                             src/game/{GameApi,Structs,Ids} (struct offsets from fishguy6564/MK8DX-Headers),
                             src/hooks/{SocketInit,PlayReport,SaveLoad,RaceFinish,CupGate,ItemGate,
                                        KartGuard,SpeedCap}Hook.cpp + IdTableDump + SdkSymbolDump,
                             tests/ (host tests: test_pattern, test_protocol, test_json)
scripts/                     build_switchmod.py, build_release.py, release_audit.py, deploy_mod.py,
                             patch_hakkun.py, setup_sail_winpath.py, install_apworld.py,
                             run_client.py, gen_apworld_data.py, gen_symbol_table.py, make_pattern.py,
                             switch_smoke_test.py, ap_generate.py, ap_server.py
docs/                        first-time-setup.md, install-switch.md, changing-servers.md, release-process.md,
                             architecture.md, wire-protocol.md, symbols.md (4.0.1 table + on-game findings),
                             porting-notes.md (Ro's 3.0.5 -> 4.0.0 method, 4.0.0 -> 4.0.1)
vendor/Archipelago           submodule pinned at 799e0b7; custom_worlds/mk8dx.apworld is gitignored
```

## Commands

```pwsh
.venv\Scripts\python -m pytest apworld\mk8dx_archipelago\tests -q -p no:cacheprovider
.venv\Scripts\python scripts\gen_apworld_data.py ; .venv\Scripts\python scripts\gen_symbol_table.py --check
python scripts\build_switchmod.py ; python scripts\deploy_mod.py ryujinx
python scripts\build_release.py            # dist/mk8dx.apworld (+ .sha256), audited
```

Host tests (msys2 mingw64 g++ on PATH, `-DMK8AP_HOST_TEST`): see
`.github/workflows/test.yml`. The `.venv` is Python 3.12 with AP deps,
pytest and yt-dlp but NO Kivy: GUI runs go through the frozen AP in
`C:\ProgramData\Archipelago` (copy the zip to `custom_worlds`, run
`ArchipelagoLauncher.exe "MK8DX Client"`, set `MK8AP_APPDATA_ROOT` to
sandbox setup state; remove the zip afterwards). Bash heredocs containing apostrophes fail in this
environment: write edit scripts to the scratchpad or use the Write tool.

## External references

* RoGamer97/ASM-Cheats-and-Patches `Switch/Mario Kart 8 Deluxe/4.0.1/`
  (and `4.0.0/`): commented ASM with per-version addresses and full enum lists (CPU Combo
  Modifier). Fetch files with `gh api .../contents/<path> --jq .content |
  base64 -d`; a plain clone fails on Windows path lengths.
* fishguy6564/MK8DX-Base-Plugin `symbols.toml` (patterns) and
  fishguy6564/MK8DX-Headers (struct layouts; `ui/Page/Page_CourseBase.hpp`,
  `gear/Player/*.hpp`, `ui/Buttons/*.hpp` were the useful ones).
* Ro's stream "Porting MK8D patches to v4.0.0" (video id 9SUvZHHfOEc):
  `docs/porting-notes.md`.
