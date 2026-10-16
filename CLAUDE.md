# CLAUDE.md - MK8DX Archipelago

Fast-load brief for the **Mario Kart 8 Deluxe** Archipelago project. Sibling
of `smo_archipelago` and `smbw_ap` (same two-tier shape: LibHakkun subsdk9
on the Switch, Kivy client inside the apworld on the PC).

| Identifier | Value |
|---|---|
| AP game name | `Mario Kart 8 Deluxe` |
| apworld zip | `mk8dx.apworld` (imports as `worlds.mk8dx`; host.yaml key `mk8dx_options`) |
| In-repo folder | `apworld/mk8dx_archipelago/` |
| Title id | `0100152000022000` |
| Target version | **4.0.0 only** (main build id `2C336A9BCF79C3040CE506CDD391B578`). User decision 2026-09-27: no 3.0.5 support, no provision for later versions. The mod stays inactive on any other build. |
| Ports | TCP 17778 (bridge), UDP 17779 (discovery). SMO uses 17777/17776. |

## Never commit Nintendo IP

No NSO/NSP/XCI dumps, no `.text` dumps (`make_pattern.py` input), no
extracted romfs. Byte patterns of a few dozen bytes and function
addresses are fine (they are the same data every public patch carries).
Course / character names are the community-standard names already used
by the MK8DX Manual world and the MKWii apworld.

## Load-bearing design points

* **One build, checked at boot.** `sym::resolveAll()` reads main's build
  id; if it is not 4.0.0, `hkMain` returns before installing anything.
* **No symbols, no sail.** `switch-mod/syms/mk8dx_symbols.toml` ->
  `scripts/gen_symbol_table.py` -> `src/sym/SymbolTable.inc` (committed).
  Entries have a published 4.0.0 `address` (from RoGamer97's 4.0.0 patch
  sources) or a `pattern`. Patterns were authored on 3.0.5, so the
  resolver tries exact bytes, then a relaxed form (branch distances and
  struct-offset immediates wildcarded) that must match exactly once.
  Provenance and the 3.0.5 -> 4.0.0 change list: `docs/symbols.md`,
  `docs/porting-notes.md`.
* **Hooks are function-entry trampolines.** That is why 4.0.0's register
  reshuffles inside functions (which broke several of Ro's mid-function
  patches) do not matter here. Do not add mid-function hooks.
* **Enum ids are learned, not hard-coded.** The public headers leave
  `ECourseID` / `EDriverID` / ... empty. The mod ships `id_table`
  (`mush::E*ID::text_`); course ids are learned by pairing a hook
  `race_result` (numeric id) with the PlayReport (course string) of the
  same race. `client/learned.py` persists them. `tables.py` `default_id`
  values are guesses that the runtime tables override.
* **`ui::CourseInfo` has no name string.** An earlier header revision
  listed one; reading it would dereference garbage.
* **Race results come from the game's own PlayReport** (nn::prepo IPC hook,
  MessagePack decoded in `client/play_report.py`). `onFakeGoal` is the
  fallback. Never hook `PlayReport::Add/Save` (delayed SDK abort; see
  smbw_ap).
* **State is a full snapshot** (`state` message), re-sent on every change.
  No item replay, no double counting. `enabled=false` / no state = vanilla.
* **Networking starts once the game is up,** from `gear::SaveDataFile::load`
  or, if that did not resolve, from the first menu / race / PlayReport hook
  (`hooks::startNetworkingOnce`). Never from `hkMain`. `SocketInitHook`
  only initialises `nn::socket` itself if the game has not.
* **Still to verify on a real 4.0.0:** which patterns resolve,
  `EGrandPrixID == ECup-1` (CupGateHook logs both), `KartVehicleMove`
  speed offsets (SpeedCapHook logs raw floats and disables itself if they
  look wrong), the `RaceKartInfo` rewrite at `SetNextCourse` taking effect.

- **Game "enums" travel by const reference (4.0.0, verified 2026-10-16).**
  `mush::E*ID`, `ui::ECup`, `ui::DLCState` are class wrappers; hooked
  functions get a pointer to a 32-bit value in x1/x2. Never declare a
  pattern-located hook's integer arguments as `int`: forward x1..x5 as
  `u64`, dereference when the register looks like a pointer, and override
  by pointing `orig()` at a local copy. Declaring them `int` truncated the
  pointers and crashed the Prepare Thread at boot (see `CupGateHook.cpp`).
- **`ui::ECup` is 0-based and equals `EGrandPrixID`** (Mushroom = 0 ...
  Spiny = 23). The header's "Mushroom = 1" reading is wrong for 4.0.0.
- **SDK 4.4.0 prepo proxy names.** MK8DX's nnSdk exports
  `CmifProxyImpl<IPrepoService, CmifDomainProxy<...MessageType 4...>,
  MemoryResourceAllocationPolicy, IPrepoService>::_nn_sf_sync_SaveReport[WithUser]`
  with no `ProcessModifierImpl` suffix. `SdkSymbolDump.cpp` logs matching
  dynsym names at boot; use it before guessing any other SDK mangled name.
- **Loadout source of truth is `gear::Player::mKartInfo`** (`Player+0x210` via
  `GetPlayerManager()`), not `RaceInfo`: the UI copies Player -> RaceInfo when the
  race is assembled and the karts are built from that copy. Rewriting either
  RaceInfo at any hook point changed nothing in the race; rewriting the Player's
  kart info works (KartGuardHook::enforcePlayers). Keep the RaceInfo rewrites as
  belt-and-braces only.
- **Cup gate lives in `Page_CourseBase::isDLC`**, a static returning a class with a
  user-declared destructor -> hidden x8 sret. The hook needs the asm stubs in
  CupGateHook.cpp; a plain lambda cannot see x8. Do not gate via
  `FlagAccessor::isOpen` (soft-lock) or via `setCup`'s state (display only).
  `Locked(4)` = padlock + cursor skips the cup; that is the intended UX.
- **Engine class is deliberately not restricted** (user decision 2026-10-16): the
  speed restriction is the top-speed clamp only. Do not add an engine-class gate.
- **`gear::DLCManager::getCupStatus` is unresolved on 4.0.0** (relaxed
  pattern not unique). Cup gating runs on `FlagAccessor::isOpen` +
  `Control_CupButton::setCup` only.
- **Ryujinx logs every PlayReport as JSON** (`ServicePrepo ProcessPlayReport`
  in `Desktop
yujinx-1.3.3\Logs`). Use it to cross-check the mod's
  MessagePack capture; real hardware only has the mod's capture.

## Layout

```
apworld/mk8dx_archipelago/   __init__.py (MK8DXWorld + Launcher Component), Data/Items/Locations/
                             Regions/Rules/Options/Helpers/Meta/DataValidation.py (Manual template),
                             hooks/{Options,World,Rules,...}.py, data/*.json (generated),
                             client/{main,context,gui,switch_server,discovery,protocol,play_report,
                                     learned,resolver,state,tables,net_util,logging_setup}.py, tests/
switch-mod/                  CMakeLists.txt, config/, syms/mk8dx_symbols.toml, sys/ (LibHakkun),
                             src/{main.cpp, ap/, game/, hooks/, sym/, util/}, tests/ (host tests)
scripts/                     build_switchmod.py, patch_hakkun.py, setup_sail_winpath.py, install_apworld.py,
                             run_client.py, gen_apworld_data.py, gen_symbol_table.py, make_pattern.py,
                             switch_smoke_test.py, ap_generate.py, ap_server.py
docs/                        architecture.md, wire-protocol.md, symbols.md, porting-notes.md
```

## Commands

```pwsh
.venv\Scripts\python -m pytest apworld\mk8dx_archipelago\tests
python scripts\gen_apworld_data.py ; python scripts\gen_symbol_table.py
python scripts\build_switchmod.py -DBRIDGE_HOST=<LAN IP>
Copy-Item switch-mod\build\exefs\* "$env:AppData\Ryujinx\mods\contents\0100152000022000\exefs\"
python scripts\install_apworld.py ; .venv\Scripts\python scripts\run_client.py --connect localhost:38281 --name Mario
```

Host tests (msys2 mingw64 g++ on PATH): see `.github/workflows/test.yml`.
