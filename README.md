# Mario Kart 8 Deluxe for Archipelago

An [Archipelago](https://archipelago.gg) world for **Mario Kart 8 Deluxe**
(Nintendo Switch, version **4.0.0 only**) with a Switch-side mod: race
results are reported automatically and cups, characters, vehicle parts,
item-box items and top speed are locked until the matching AP items arrive.

Runs on Ryujinx and on a modded Switch (Atmosphere). No emulator memory
reading: the mod talks to the PC client over LAN.

## Status

Alpha. Built, host-tested, and the PC side is verified end to end over
loopback. The Switch mod has not yet run against a real 4.0.0 copy; see
*First run checklist*. `docs/symbols.md` lists which game functions have a
published 4.0.0 address and which are still located by pattern.

The mod checks the game's build id at boot. On anything other than 4.0.0
(`2C336A9BCF79C304...`) it logs an error and stays inactive.

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

1. Ryujinx log: build id `2C336A9B...`, then one `[sym]` line per game
   function and `[sym] resolved N/17`. Note any `UNRESOLVED` or
   `RELAXED pattern` lines; the client also lists what is missing and which
   feature it affects when the Switch connects.
2. Reach the menu or load a save: `[net] starting networking`,
   `[conn] CONNECTED`, and the client logs `switch HELLO`.
3. Race one VS course: the client logs `play_report race: ...` and checks
   `<course> - Finish`.
4. Cup select: `[cup]` log lines show `EGrandPrixID` values; confirm they
   match `ECup - 1` (docs/symbols.md).
5. `[speed] raw:` line shows sane floats (top speed 0..100).

## Tests

```bash
.venv/Scripts/python -m pytest apworld/mk8dx_archipelago/tests      # Python
MK8AP_LIVE_AP=1 .venv/Scripts/python -m pytest apworld/mk8dx_archipelago/tests/test_apworld_generation.py
# C++ host tests: see .github/workflows/test.yml (g++ from msys2 mingw64)
```

## License

MIT. Symbol addresses and patterns are data derived from public community
research; see `docs/symbols.md` for attribution.
