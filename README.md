# Mario Kart 8 Deluxe for Archipelago

An [Archipelago](https://archipelago.gg) world for **Mario Kart 8 Deluxe**
(Nintendo Switch, version **3.0.5**) with a Switch-side mod: race results
are reported automatically and cups, characters, vehicle parts, item-box
items and top speed are locked until the matching AP items arrive.

Runs on Ryujinx and on a modded Switch (Atmosphere). No emulator memory
reading: the mod talks to the PC client over LAN.

## Status

Alpha. Built and host-tested; on-game verification of the hooks against a
real 3.0.5 copy is the next step (see *First run* below). The design notes
in `docs/` and the plan of record describe what is verified and what is
still an assumption.

## Layout

| Path | What |
|---|---|
| `apworld/mk8dx_archipelago/` | the apworld (Manual-template generator + data) and the Kivy client under `client/` |
| `switch-mod/` | LibHakkun subsdk9: pattern-resolved hooks + TCP bridge |
| `scripts/` | `build_switchmod.py`, `install_apworld.py`, `gen_apworld_data.py`, `gen_symbol_table.py`, `make_pattern.py`, `switch_smoke_test.py`, `ap_generate.py`, `ap_server.py` |
| `docs/` | architecture, wire protocol, symbol provenance |
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
```

Connect the client to the room, boot the game with the mod. The client's
*Kart* tab shows the Switch link, unlocked cups/characters/items, current
top speed and recent races. Locations: per course *Finish*, *Top 3*,
*Win* (Grand Prix or VS), and *Complete \<cup\> (Grand Prix)*; goal:
complete `goal_cups` cups.

## First run checklist (on-game verification)

1. Ryujinx log: `[sym] resolved N/M` with the 3.0.5 build id; every hook
   line says `installed`.
2. Load a save: `[net] worker thread started`, `[conn] CONNECTED`, and the
   client logs `switch HELLO`.
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

MIT. Symbol patterns and offsets are data derived from public community
research; see `docs/symbols.md` for attribution.
