# Symbols: how the mod finds game functions, and where the data comes from

The mod supports exactly one game build: **MK8DX 4.0.0**, main build id
`2C336A9BCF79C3040CE506CDD391B578`. On any other build `hkMain` logs an
error and installs nothing.

MK8DX ships no symbols. `switch-mod/syms/mk8dx_symbols.toml` lists every
game function the mod hooks or calls. `src/sym/PatternResolver.cpp` locates
each one at boot:

| Locator | Used for | How |
|---|---|---|
| `address` | functions with a published 4.0.0 address | used as-is |
| `pattern` + `offset`, exact | everything else | byte pattern scanned over main `.text` at instruction alignment; must match once |
| `pattern`, relaxed | when the exact bytes miss | branch distances, ADRP pages and struct-offset immediates wildcarded; must match exactly once |

The patterns were authored against 3.0.5, which is why the relaxed pass
exists: between 3.0.5 and 4.0.0 most code is byte-identical, but relative
branches always differ and some struct members moved. When an entry has
both an address and a pattern, the pattern result is logged next to the
address; that tells you how well the 3.0.5-era patterns carry over and so
how much to trust the pattern-only entries.

The boot log prints one line per entry (`published address`, `pattern`,
`RELAXED pattern` or `UNRESOLVED`) and `resolved N/M`. A hook whose symbol
is unresolved skips itself. The `hello` message carries the counts and the
unresolved names, and the PC client says which features are off.

SDK functions (`nn::socket`, `nn::nifm`, `nn::os`, `nn::prepo`) are not in
this table: they are exported by the game / sdk modules and resolved by
name through nnrtld (`HkTrampoline::installAtSym`,
`--unresolved-symbols=ignore-all`).

## Current table (4.0.0)

| Function | Locator |
|---|---|
| `gear::GetRaceInfo` | address `0x87FCD0` |
| `gear::FrameworkUtil::getCurrentGameScene` | address `0x7F7AC0` |
| `gear::NetworkUtil::getMyKartIndex` | address `0x862820` |
| `ui::RaceWindow::onFakeGoal` | address `0x550168` (Ro hooks `+0xD8` at `0x550240`) |
| `gear::ItemOwner::startSlot` | address `0x4336C` |
| `gear::GetNextRaceInfo` | pattern, exact hit `+0x87FCE8` (verified 2026-10-16) |
| `gear::FlagAccessor::isOpen` | pattern, exact hit `+0x876370`; fires per cup on the title-screen cup grid |
| `gear::DLCManager::getCupStatus` | pattern **UNRESOLVED** on 4.0.0 (no exact hit, relaxed form not unique). Layer 2 of the cup gate is skipped; layers 1 and 3 cover it |
| `ui::SetNextCourse` | pattern, RELAXED hit `+0x41FCF8` (the `#0x1744 -> #0x1A44` member move, as predicted) |
| `ui::Control_CupButton::setCup` | pattern, exact hit `+0x43B9FC` |
| `ui::Control_RaceLapCoin::onCalc_` | pattern, exact hit `+0x512C84` |
| `gear::SaveDataFile::load` | pattern, exact hit `+0x8EB2D8`; fires twice at boot |
| `gear::GetCurrentUserSaveData` | pattern, exact hit `+0x8BD538` |
| `mush::E{Driver,Body,Tire,Wing}ID::text_` | pattern, exact hits `+0xB4FF0 / +0xB5384 / +0x16B86C / +0x13317C`; tables enumerate 54 / 42 / 23 / 16 entries |

First boot on a real 4.0.0 dump (Ryujinx 1.3.3, 2026-10-16): build id
`2C336A9BCF79C3040CE506CDD391B578` matched, 16/17 resolved.

### Calling-convention findings from that boot

* `mush::E*ID`, `ui::ECup` and `ui::DLCState` are class wrappers and are
  passed **by const reference**: the hooked functions receive a pointer to
  a 32-bit value on the caller's stack in x1/x2. Declaring those arguments
  as `int` truncated the pointers inside `orig()` and crashed the Prepare
  Thread at boot. Every pattern-located hook now forwards x1..x5 as `u64`
  and dereferences only when the register looks like a pointer.
* `ui::ECup` is **0-based** (Mushroom = 0 ... Spiny = 23) and equal to
  `mush::EGrandPrixID`: the cup grid calls `isOpen(gp=N)` then
  `setCup(ecup=N)` for each cup. BCP cups the save does not own arrive as
  `DLCState` 2 (NotBuy).
* The nn::prepo IPC proxy symbols on MK8DX's NintendoSDK 4.4.0 are
  `CmifProxyImpl<IPrepoService, CmifDomainProxy<...MessageType 4...>,
  MemoryResourceAllocationPolicy, IPrepoService>` with no
  `ProcessModifierImpl` suffix (SDK 15+ names differ). `SdkSymbolDump.cpp`
  logs the exact mangled names at boot; both SaveReport hooks install.
* Ryujinx itself prints every PlayReport as JSON (`ServicePrepo
  ProcessPlayReport`) in its log, which doubles as a cross-check for the
  mod's MessagePack capture during emulator testing.

## What changed in 4.0.0 (from Ro's port)

Checked against every 3.0.5 / 4.0.0 `source.s` pair on 2026-09-27:

* Code moved by roughly `+0x3A00`; all function addresses differ.
* `object::DriverKart` members shifted by 8 (`0x1AC -> 0x1B4`,
  `0x324 -> 0x32C`, `0x334 -> 0x33C`). Not used by this mod.
* UI page ids shifted by one (`PAGEID_RACE 0x6E -> 0x6F`). Not used.
* A UI struct member moved `0x1744 -> 0x1A44`. Only matters for patterns.
* Register allocation changed inside `gear::ItemOwner::calcKeyInput_`.
  This mod hooks `startSlot` at function entry, where the ABI fixes the
  argument registers, so it is unaffected.
* Unchanged and used here: the scene chain (`+0x1B0` ObjectEngine,
  `+0x218` RaceDirector, `+0x238` KartDirector, `+0xC8` kart holders),
  `RaceKartChecker + 0x40` rank, `RaceInfo + 0x8 / + 0x180`,
  `ItemOwner + 0x48 -> + 0x48 -> KartVehicle + 0xD0` (is local player),
  `startSlot(int, EItemSlot, bool, bool)`.

## Provenance and attribution

* Addresses and pointer chains: commented ASM of
  [RoGamer97/ASM-Cheats-and-Patches](https://github.com/RoGamer97/ASM-Cheats-and-Patches)
  (`Switch/Mario Kart 8 Deluxe/4.0.0/*/source.s`). Those patches are
  CC BY-NC-ND; only addresses and function names are used, no code.
* Porting method: Ro's stream "Porting MK8D patches to v4.0.0 (Tutorial
  and Tips)" (2026-09-25). See `docs/porting-notes.md`.
* Patterns: copied as data from
  [fishguy6564/MK8DX-Base-Plugin](https://github.com/fishguy6564/MK8DX-Base-Plugin)
  `symbols.toml` (GPLv2 repository; none of its code is used). Struct
  offsets in `src/game/Structs.hpp`:
  [fishguy6564/MK8DX-Headers](https://github.com/fishguy6564/MK8DX-Headers).
* The PlayReport IPC hook shape follows the `smbw_ap` project.

## Replacing a pattern with an address

Once a 4.0.0 dump is on disk, any pattern-only entry can be pinned:

1. Boot once and read the resolved offset from the log, or find the
   function in Ghidra / IDA.
2. Add `address = "0x..."` to its entry in `syms/mk8dx_symbols.toml`.
3. `python scripts/gen_symbol_table.py`, rebuild.

`scripts/make_pattern.py` does the reverse (address to unique pattern).
Never commit the dump itself (`*.nso`, `*.text.bin` are gitignored).

## Known gaps

* The character / kart select pages are not hooked, so locked characters
  are swapped at race start rather than greyed out in the menu.
* `EGrandPrixID` is assumed to equal `ECup - 1`; `CupGateHook` logs the
  ids it sees so this can be confirmed on first boot.
* `ui::CourseInfo` carries no course name. Course ids for the hook channel
  are learned by pairing a hook `race_result` with the PlayReport of the
  same race; the PlayReport channel needs no ids at all.
