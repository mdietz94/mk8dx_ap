# Mario Kart 8 Deluxe for Archipelago

An [Archipelago](https://archipelago.gg) world for **Mario Kart 8 Deluxe**
(Nintendo Switch, version **4.0.0 only**) with a Switch-side mod: race
results are reported automatically and cups, characters, vehicle parts,
item-box items and top speed are locked until the matching AP items arrive.

Runs on Ryujinx and on a modded Switch (Atmosphere). No emulator memory
reading: the mod talks to the PC client over LAN.

## Status

Alpha, played against a live seed on a real 4.0.0 dump under Ryujinx
(2026-10-16). Verified in game: boot and symbol resolution (20 of 21
functions), race results via the game's own PlayReport, Archipelago checks
and item receipt, item-box gating, character/vehicle enforcement, cup
locking (padlock, cursor skips the cup) and the top-speed clamp. Not yet
verified: completing a Grand Prix (Trophy / goal) and real Switch hardware.
`docs/symbols.md` lists every game function with its 4.0.0 location and the
on-game findings.

The mod checks the game's build id at boot. On anything other than 4.0.0
(`2C336A9BCF79C304...`) it logs an error and stays inactive. The engine
class picker is intentionally not restricted; speed is limited by a
per-frame top-speed clamp that rises with Progressive Speed items.

## Layout

| Path | What |
|---|---|
| `apworld/mk8dx_archipelago/` | the apworld (Manual-template generator + data) and the Kivy client under `client/` |
| `switch-mod/` | LibHakkun subsdk9: resolved hooks + TCP bridge |
| `scripts/` | `build_switchmod.py`, `install_apworld.py`, `run_client.py`, `gen_apworld_data.py`, `gen_symbol_table.py`, `make_pattern.py`, `switch_smoke_test.py`, `ap_generate.py`, `ap_server.py` |
| `docs/` | architecture, wire protocol, symbol provenance, porting notes |
| `vendor/Archipelago` | Archipelago submodule (pinned) |

## Build

```bash
git submodule update --init --recursive
python scripts/gen_symbol_table.py          # only after editing syms/*.toml
python scripts/build_switchmod.py -DBRIDGE_HOST=<your PC LAN IP>
```

Output: `switch-mod/build/exefs/{subsdk9,main.npdm}`. Deploy to
`%AppData%\Ryujinx\mods\contents\0100152000022000\exefs\` (Ryujinx) or
`sd:/atmosphere/contents/0100152000022000/exefs/` (Switch).

Toolchain (Windows): LLVM 19, CMake, Ninja, Python 3.12, msys2 mingw64
(for sail + host tests). Same as `smo_archipelago`; `build_switchmod.py`
applies the LibHakkun Windows patches automatically.

## Play

```bash
python scripts/install_apworld.py           # -> vendor/Archipelago/custom_worlds/mk8dx.apworld
python vendor/Archipelago/Launcher.py       # Generate with a "Mario Kart 8 Deluxe" yaml, host, then "MK8DX Client"
# or, without the Launcher (dev / headless):
.venv/Scripts/python scripts/run_client.py --connect localhost:38281 --name Mario [--nogui]
python scripts/switch_smoke_test.py --course Gu_FirstCircuit --rank 1 --rule VS   # fake Switch
```

Connect the client to the room, boot the game with the mod. The client's
*Kart* tab shows the Switch link, unlocked cups/characters/items, current
top speed and recent races. Locations: per course *Finish*, *Top 3*,
*Win* (Grand Prix or VS), and *Complete \<cup\> (Grand Prix)*; goal:
complete `goal_cups` cups.

## First run checklist (on-game verification)

1. Ryujinx log: build id `2C336A9B...`, one `[sym]` line per game function
   and `[sym] resolved 20/21` (`gear::DLCManager::getCupStatus` is the
   expected `UNRESOLVED`). `grep InvalidAccessHandler` first: a crash prints
   registers and a guest stack trace.
2. Reach the menu or load a save: `[net] starting networking`,
   `[conn] CONNECTED`, and the client logs `switch HELLO`.
3. Race one course: the client logs `play_report race: ...`, learns the
   course id from the paired `race_result`, and checks `<course> - Finish`.
4. Cup select: AP-locked cups show a padlock and cannot be selected
   (`[cup] Page_CourseBase::isDLC(ecup=N) vanilla=5 -> 4`).
5. Pick a locked character: `[kart] player 0 driver N locked -> M` and the
   race uses the starting loadout.
6. `[speed] clamping to N%` once the race starts; `[item] local kart rolled
   locked item` on a locked roll.

## Tests

```bash
.venv/Scripts/python -m pytest apworld/mk8dx_archipelago/tests      # Python
MK8AP_LIVE_AP=1 .venv/Scripts/python -m pytest apworld/mk8dx_archipelago/tests/test_apworld_generation.py
# C++ host tests: see .github/workflows/test.yml (g++ from msys2 mingw64)
```

## License

MIT. Symbol addresses and patterns are data derived from public community
research; see `docs/symbols.md` for attribution.
