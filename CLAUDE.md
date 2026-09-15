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
| Target version | **3.0.5** (build id `FE941ED5BA14BE5D...`). 3.0.3 and below are ARM32 and cannot load the mod; 4.0.0 (2026-09) has no published addresses yet. |
| Ports | TCP 17778 (bridge), UDP 17779 (discovery). SMO uses 17777/17776. |

## Never commit Nintendo IP

No NSO/NSP/XCI dumps, no `.text` dumps (`make_pattern.py` input), no
extracted romfs. Byte patterns of a few dozen bytes and function
addresses are fine (they are the same data every public patch carries).
Course / character names are the community-standard names already used
by the MK8DX Manual world and the MKWii apworld.

## Load-bearing design points

* **No symbols, no sail.** `switch-mod/syms/mk8dx_symbols.toml` ->
  `scripts/gen_symbol_table.py` -> `src/sym/SymbolTable.inc` (committed).
  `sym::resolveAll()` scans main `.text` at boot; each hook skips itself
  if unresolved. Provenance: `docs/symbols.md`.
* **Enum ids are learned, not hard-coded.** The public headers leave
  `ECourseID` / `EDriverID` / ... empty. The mod ships `id_table`
  (`mush::E*ID::text_`) and `course_name` (`CourseInfo` icon resource)
  messages; `client/learned.py` persists them. `tables.py` `default_id`
  values are guesses that the runtime tables override.
* **Race results come from the game's own PlayReport** (nn::prepo IPC hook,
  MessagePack decoded in `client/play_report.py`). `onFakeGoal` is the
  fallback. Never hook `PlayReport::Add/Save` (delayed SDK abort; see
  smbw_ap).
* **State is a full snapshot** (`state` message), re-sent on every change.
  No item replay, no double counting. `enabled=false` / no state = vanilla.
* **Networking starts on `gear::SaveDataFile::load`** (not in hkMain): by
  then nn::nifm/socket are the game's problem; `SocketInitHook` only
  initialises `nn::socket` itself if the game has not.
* **Assumptions still to verify on a real 3.0.5:** `EGrandPrixID == ECup-1`
  (CupGateHook logs both), `KartVehicleMove` offsets (SpeedCapHook logs raw
  floats and disables itself if they look wrong), `ItemOwner::startSlot`
  4-arg signature, `RaceKartInfo` rewrite at `SetNextCourse` taking effect.

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
                             gen_apworld_data.py, gen_symbol_table.py, make_pattern.py,
                             switch_smoke_test.py, ap_generate.py, ap_server.py
docs/                        architecture.md, wire-protocol.md, symbols.md
```

## Commands

```pwsh
.venv\Scripts\python -m pytest apworld\mk8dx_archipelago\tests
python scripts\gen_apworld_data.py ; python scripts\gen_symbol_table.py
python scripts\build_switchmod.py -DBRIDGE_HOST=<LAN IP>
Copy-Item switch-mod\build\exefs\* "$env:AppData\Ryujinx\mods\contents\0100152000022000\exefs\"
python scripts\install_apworld.py ; python vendor\Archipelago\Launcher.py
```

Host tests (msys2 mingw64 g++ on PATH): see `.github/workflows/test.yml`.
