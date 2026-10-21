# Symbols: how the mod finds game functions, and where the data comes from

The mod supports exactly one game build: **MK8DX 4.0.1**, main build id
`158C0C7399AABD7BF83795C2EF3484C7`. On any other build (4.0.0 included:
`2C336A9BCF79C3040CE506CDD391B578`) `hkMain` logs an error and installs
nothing. The mod targeted 4.0.0 until 2026-10-21; 4.0.0 support was
dropped rather than kept alongside (user decision: latest version only).

MK8DX ships no symbols. `switch-mod/syms/mk8dx_symbols.toml` lists every
game function the mod hooks or calls. `src/sym/PatternResolver.cpp` locates
each one at boot:

| Locator | Used for | How |
|---|---|---|
| `address` | functions with a published 4.0.1 address | used as-is |
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

## Current table (4.0.1)

Boot on a real 4.0.1 dump (XCI base + v1507328 update, Ryujinx 1.3.3,
2026-10-21): build id `158C0C7399AABD7BF83795C2EF3484C7` matched, 20/21
resolved, every hook installed, client handshake OK. Every pattern hit
moved by the same per-region delta as Ro's published addresses (last
column), which is how the pattern hits were checked to be the right
functions.

| Function | Locator (4.0.1) | 4.0.0 | Delta |
|---|---|---|---|
| `gear::GetRaceInfo` | address `0x86F7BC` | `0x87FCD0` | -0x10514 |
| `gear::FrameworkUtil::getCurrentGameScene` | address `0x7E7434` | `0x7F7AC0` | -0x1068C |
| `gear::NetworkUtil::getMyKartIndex` | address `0x852234` | `0x862820` | -0x105EC |
| `ui::RaceWindow::onFakeGoal` | address `0x5502F0` (Ro hooks `+0xD8` at `0x5503C8`) | `0x550168` | +0x188 |
| `gear::ItemOwner::startSlot` | address `0x43374` | `0x4336C` | +0x8 |
| `ui::SetRandomCPU` | address `0x4FD858` (Ro hooks `+0x2140` at `0x4FF998`) | `0x4FD6D0` | +0x188 |
| `gear::GetNextRaceInfo` | pattern, exact hit `+0x86F7D4` | `+0x87FCE8` | -0x10514 |
| `gear::FlagAccessor::isOpen` | pattern, exact hit `+0x865E7C`; fires per cup on the title-screen cup grid | `+0x876370` | -0x104F4 |
| `gear::DLCManager::getCupStatus` | pattern **UNRESOLVED** (no exact hit, relaxed form not unique; same on 4.0.0). Layer 2 of the cup gate is skipped; layers 1 and 3 cover it | - | - |
| `ui::SetNextCourse` | pattern, RELAXED hit `+0x41FE64` (the 3.0.5 `#0x1744 -> #0x1A44` member move) | `+0x41FCF8` | +0x16C |
| `ui::Control_CupButton::setCup` | pattern, exact hit `+0x43BB68` | `+0x43B9FC` | +0x16C |
| `ui::Control_RaceLapCoin::onCalc_` | pattern, exact hit `+0x512E0C` | `+0x512C84` | +0x188 |
| `gear::SaveDataFile::load` | pattern, exact hit `+0x8DADC4`; fires twice at boot | `+0x8EB2D8` | -0x10514 |
| `gear::GetCurrentUserSaveData` | pattern, exact hit `+0x8AD024` | `+0x8BD538` | -0x10514 |
| `gear::GetPlayerManager` | pattern, exact hit `+0x8ACE28` | `+0x8BD33C` | -0x10514 |
| `ui::Page_CourseBase::isDLC` | pattern, exact hit `+0x494E44` | `+0x494CD8` | +0x16C |
| `object::KartDirector::prepareBeforeStructure` | pattern, exact hit `+0x13D778` | `+0x13D588` | +0x1F0 |
| `mush::E{Driver,Body,Tire,Wing}ID::text_` | pattern, exact hits `+0xB51E0 / +0xB5574 / +0x16BA5C / +0x13336C`; tables enumerate 54 / 42 / 23 / 16 entries (as on 4.0.0) | `+0xB4FF0 / +0xB5384 / +0x16B86C / +0x13317C` | +0x1F0 |

## What changed in 4.0.1 (from Ro's port)

Checked against every 4.0.0 / 4.0.1 `source.s` pair on 2026-10-21 (30
pairs): with addresses masked, the sources are identical except one GOT
slot (`ADRP #0x1306000; LDR X0, [X0, #0xE78 -> #0xE70]`, a `sead::Random`
pointer the mod does not use). No struct offset or pointer chain the mod
uses changed. Code moved by region: about `+0x8` low in `.text`, `+0x16C`
to `+0x1F0` in the UI / object code, `-0x105xx` from roughly `0x7E0000`
up. Relative layout inside the hooked functions is unchanged (Ro's
`onFakeGoal + 0xD8` and `SetRandomCPU + 0x2140` hook offsets are the same).

## Findings from the 4.0.0 sessions (still apply: 4.0.1 code is the same)

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
* First race (GP 50cc, Mushroom Cup race 1, 2026-10-16): `ui::SetNextCourse`
  fired with course id 27 (and 28 for the next race preview), the speed hook
  read `top=8.325/6.460/6.460` (offsets sane), `onFakeGoal` fired once per
  RaceWindow (5 windows, every auto-finished kart, not just the player; the
  hook now keeps only the kart whose KartVehicle is master), `RaceWindow+0xF4`
  does not hold the kart index on 4.0.0 (reads -1; the window-id derivation
  is used), ranks are 0-based, and the `match` PlayReport decoded to
  `Course=Gu_FirstCircuit Rank=1 Rule=GP Engine=50cc`. Course id 27 was
  learned as Mario Kart Stadium from the hook + report pairing.
* Added after the first live-seed session (2026-10-16), all pattern hits on 4.0.0:
  `ui::SetRandomCPU` (published `0x4FD6D0`), `object::KartDirector::prepareBeforeStructure`
  (`+0x13D588`), `gear::GetPlayerManager` (`+0x8BD33C`), `ui::Page_CourseBase::isDLC`
  (`+0x494CD8`). 20/21 resolve; only `DLCManager::getCupStatus` is still unresolved.
* **Loadout enforcement**: rewriting `GetNextRaceInfo()`/`GetRaceInfo()` kart infos at
  `SetNextCourse`, after `SetRandomCPU`, or even pre-orig in
  `KartDirector::prepareBeforeStructure` never changed the race (PlayReport kept the
  menu pick). The human selection lives in `gear::Player::mKartInfo` (`Player+0x210`,
  `PlayerManager+0x50` PtrArray, 16 slots, `+0x178` local count); rewriting it there
  works. RaceInfo is downstream of it.
* **Cup gate**: `FlagAccessor::isOpen` returning false soft-locks cup select (Deluxe has
  no closed-cup UI path). `Control_CupButton::setCup`'s DLCState is display only, and
  `Control_CupButton+0x3CB` is 1 for every cup (not a lock flag). The working gate is
  `static DLCState Page_CourseBase::isDLC(ECup)`: DLCState has a user-declared
  destructor, so it is returned through the hidden x8 pointer and ECup is passed by
  reference; the hook entry is an asm stub (`mk8ap_isDlcEntry`) that forwards x8.
  Returning `Locked(4)` (the pre-release Booster Course Pass state) draws a padlock and
  makes the cup unselectable: the cursor skips it.
* **Speed**: by user decision (2026-10-16) the engine-class picker is NOT gated; the
  restriction is the top-speed clamp scaled by Progressive Speed. `engine_max` in the
  state message stays informational.
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
  (`Switch/Mario Kart 8 Deluxe/4.0.1/*/source.s`). Those patches are
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

Once a 4.0.1 dump is on disk, any pattern-only entry can be pinned:

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
