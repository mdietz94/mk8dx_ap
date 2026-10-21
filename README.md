# Mario Kart 8 Deluxe for Archipelago

An [Archipelago](https://archipelago.gg) world for **Mario Kart 8 Deluxe**
(Nintendo Switch, version **4.0.1 only**) with a Switch-side mod: race
results are reported automatically and cups, characters, vehicle parts,
item-box items and top speed are locked until the matching AP items arrive.

Runs on Ryujinx and on a modded Switch (Atmosphere). No emulator memory
reading: the mod talks to the PC client over LAN.

## Install (players)

1. Download `mk8dx.apworld` from the
   [releases](https://github.com/mdietz94/mk8dx_ap/releases) into Archipelago's
   `custom_worlds` folder.
2. Open **MK8DX Client** from the Archipelago Launcher. The setup wizard opens
   on the first run (`/setup` later): pick Ryujinx, a Switch SD card or a
   folder and click **Install**. The Switch mod ships prebuilt inside the
   apworld, verified against a SHA-256 manifest; nothing to compile, no game
   data to extract. Setup writes this PC's LAN address next to the mod
   (`romfs/ap_config.json`) and the Switch sweeps that /24 to find the client,
   so one binary works on every network.
3. Start the game (update 4.0.1) and connect the client to your room.

Guides: [first-time setup](docs/first-time-setup.md),
[real Switch](docs/install-switch.md), [changing servers](docs/changing-servers.md).

## Status

Alpha, played against a live seed on a real 4.0.0 dump under Ryujinx
(2026-10-16). Verified in game: boot and symbol resolution (20 of 21
functions), race results via the game's own PlayReport, Archipelago checks
and item receipt, item-box gating, character/vehicle enforcement, cup
locking (padlock, cursor skips the cup) and the top-speed clamp. Not yet
verified: completing a Grand Prix (Trophy / goal) and real Switch hardware.
The installer (prebuilt mod, setup wizard, release build + audit) was tested
2026-10-18 from a frozen Archipelago 0.6.7 install: wizard -> Ryujinx deploy
-> game boots -> the mod reads `ap_config.json` at `nn::fs::MountRom` and
finds the client by sweeping that address's /24.
Moved to game update 4.0.1 on 2026-10-21 (boot, 20 of 21 symbols, every
hook installed and the client handshake re-checked on 4.0.1; races were
played on 4.0.0, whose code 4.0.1 keeps unchanged apart from addresses).
`docs/symbols.md` lists every game function with its 4.0.1 location and the
on-game findings.

The mod checks the game's build id at boot. On anything other than 4.0.1
(`158C0C7399AABD7B...`, so 4.0.0 too) it logs an error and stays inactive. The engine
class picker is intentionally not restricted; speed is limited by a
per-frame top-speed clamp that rises with Progressive Speed items.

## Layout

| Path | What |
|---|---|
| `apworld/mk8dx_archipelago/` | the apworld (Manual-template generator + data) and the Kivy client under `client/` |
| `switch-mod/` | LibHakkun subsdk9: resolved hooks + TCP bridge |
| `apworld/mk8dx_archipelago/_setup/` | setup wizard (Kivy), headless CLI, deploy targets, bundled-mod verification |
| `scripts/` | `build_switchmod.py`, `build_release.py`, `install_apworld.py`, `release_audit.py`, `deploy_mod.py`, `run_client.py`, `gen_apworld_data.py`, `gen_symbol_table.py`, `make_pattern.py`, `switch_smoke_test.py`, `ap_generate.py`, `ap_server.py` |
| `docs/` | architecture, wire protocol, symbol provenance, porting notes |
| `vendor/Archipelago` | Archipelago submodule (pinned) |

## Build

```bash
git submodule update --init --recursive
python scripts/gen_symbol_table.py          # only after editing syms/*.toml
python scripts/build_switchmod.py           # no network settings needed
python scripts/deploy_mod.py ryujinx        # or: sd --root E:/  |  folder --root <dir>  |  status
python scripts/build_release.py             # release: mod + bundled apworld + audit + sha256 -> dist/
```

Output: `switch-mod/build/exefs/{subsdk9,main.npdm}`; `deploy_mod.py` copies
them with the wizard's code to
`%AppData%\Ryujinx\mods\contents\0100152000022000\exefs\` or
`sd:/atmosphere/contents/0100152000022000/exefs/`, plus
`romfs/ap_config.json` with this PC's LAN address (`--bridge-host` to
override). Nothing network-specific is compiled in
(`scripts/release_audit.py` rejects a mod with a baked address). Releases:
`docs/release-process.md`.

Toolchain (Windows): LLVM 19, CMake, Ninja, Python 3.12, msys2 mingw64
(for sail + host tests). Same as `smo_archipelago`; `build_switchmod.py`
applies the LibHakkun Windows patches automatically.

## Play

```bash
python scripts/install_apworld.py           # -> vendor/Archipelago/custom_worlds/mk8dx.apworld (add --bundle-mod for the wizard)
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
