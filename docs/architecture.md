# Architecture

Two tiers, same shape as `smo_archipelago` / `smbw_ap`:

```
Switch / Ryujinx                                   PC
+-------------------------------+   TCP :17778   +---------------------------+   websocket   +-----------+
| MK8DX 4.0.1 + subsdk9 (mod)   | <------------> | MK8DX Client (Kivy)       | <-----------> | AP server |
|  hooks -> ApState rings       |   line JSON    |  SwitchServer + Discovery |               +-----------+
|  ApClient worker (nn::socket) |   UDP :17779   |  MK8DXContext (CommonCtx) |
+-------------------------------+                +---------------------------+
```

## Switch mod (`switch-mod/`, LibHakkun subsdk9)

* `hkMain` checks the build id is 4.0.1 (otherwise the mod stays
  inactive), resolves game symbols (`sym/PatternResolver`), binds
  `game/GameApi`, installs SDK hooks (socket init observer, PlayReport IPC
  capture) and game hooks. Networking starts from the first
  `gear::SaveDataFile::load`, or from the first menu / race / PlayReport
  hook if that function was not located.
* Threads: game threads run the hooks and only touch `ApState` (atomics +
  SPSC rings); one nn::os worker thread does discovery, TCP, HELLO, and
  pumps the rings.
* Hooks (`src/hooks/`):
  * `PlayReportHook`: race results from the game's own telemetry (primary).
  * `RaceFinishHook`: `ui::RaceWindow::onFakeGoal` (fallback; rank via
    `RaceKartChecker+0x40`).
  * `CupGateHook`: `FlagAccessor::isOpen`, `DLCManager::getCupStatus`,
    `Control_CupButton::setCup` return "locked" for AP-locked cups.
  * `ItemGateHook`: `ItemOwner::startSlot` remaps locked items to Coin.
  * `KartGuardHook`: `ui::SetNextCourse` rewrites locked driver/parts on
    local karts to the starting loadout.
  * `SpeedCapHook`: per-frame clamp of `KartVehicleMove::mSpeed` to
    `top * speed_pct`.
  * `IdTableDump`: `mush::E*ID::text_` tables shipped once so the PC learns
    the enum ids.

## PC client (`apworld/mk8dx_archipelago/client/`)

* `switch_server.py`: TCP server; answers HELLO with `hello_ack` + `state`,
  routes `play_report` / `race_result` to the context, learns id tables.
* `context.py`: `CommonContext` subclass; received items -> `state.py`
  snapshot -> `state` push; race events -> `resolver.py` -> LocationChecks;
  `GrandPrixTracker` credits "Complete X Cup" after four GP races; goal =
  N cups.
* `learned.py`: id tables persisted to `mk8dx_learned_ids.json` in the
  Archipelago user dir.
* `tables.py`: single source of truth for names; `scripts/gen_apworld_data.py`
  renders the apworld JSON from it.

## Apworld (`apworld/mk8dx_archipelago/`)

Manual-template generation code (Data/Items/Locations/Regions/Rules) with
MK8DX data. Regions = cups (require the cup item); locations = per course
Finish / Top 3 / Win + "Complete <cup> (Grand Prix)" holding a locked
`Trophy`; goal = `Trophy x goal_cups`. Items: cups, characters, karts,
tires, gliders, item-box items, Progressive Speed, Coin Bonus filler.
