# Symbols: how the mod finds game functions, and where the data comes from

MK8DX ships no symbols. `switch-mod/syms/mk8dx_symbols.toml` lists every
game function the mod hooks or calls with up to two locators, tried in
order at boot by `src/sym/PatternResolver.cpp`:

1. **Byte pattern + offset.** IDA-style pattern (`??` wildcards) scanned
   over the main module's `.text`; address = first match + signed offset.
   Version-tolerant as long as the code sequence survives a rebuild.
2. **`offset_305`.** Absolute main-relative offset, used only when the main
   build id starts with `FE941ED5BA14BE5D` (MK8DX 3.0.5).

The boot log prints every entry as `pattern`, `3.0.5 offset` or
`UNRESOLVED`, plus `resolved N/M`. A hook whose symbol is unresolved skips
itself; nothing aborts. The `hello` message carries `resolved/total` so the
PC client can warn.

SDK functions (`nn::socket`, `nn::nifm`, `nn::os`, `nn::prepo`) are not in
this table: they are exported by the game / sdk modules and resolved by
name through nnrtld (`HkTrampoline::installAtSym`, `--unresolved-symbols=ignore-all`).

## Provenance and attribution

* Patterns and offsets marked `pattern` are copied as data from
  [fishguy6564/MK8DX-Base-Plugin](https://github.com/fishguy6564/MK8DX-Base-Plugin)
  `symbols.toml` (GPLv2 repository; the *code* of that project is not used
  here). Struct offsets in `src/game/Structs.hpp` come from
  [fishguy6564/MK8DX-Headers](https://github.com/fishguy6564/MK8DX-Headers).
* `offset_305` values and the pointer chains (scene -> ObjectEngine ->
  RaceDirector / KartDirector, `RaceKartChecker+0x40`, `ItemOwner::startSlot`)
  come from the commented ASM of
  [RoGamer97/ASM-Cheats-and-Patches](https://github.com/RoGamer97/ASM-Cheats-and-Patches)
  (`Switch/Mario Kart 8 Deluxe/3.0.5/*/source.s`). Those patches are
  CC BY-NC-ND; only the addresses and function names are used, no code.
* The PlayReport IPC hook shape follows the user's own `smbw_ap` project.

## Adding or promoting a symbol

1. Find the function on 3.0.5 (an address from a public patch, or Ghidra).
2. Dump the main `.text` of your 3.0.5 copy and run
   `python scripts/make_pattern.py --text main.text.bin --offset 0x... --name "ns::Fn"`.
   It prints a TOML entry whose pattern matches exactly once.
3. Paste it into `syms/mk8dx_symbols.toml`, run
   `python scripts/gen_symbol_table.py`, rebuild.

Never commit the dump itself (`*.nso`, `*.text.bin` are gitignored).

## Known gaps

* `ui::Control_CourseButton::setCourse` has no locator yet (course ids are
  still learned from `ui::SetNextCourse`, one course per race).
* The character / kart select pages are not hooked, so locked characters
  are swapped at race start rather than greyed out in the menu.
* `EGrandPrixID` is assumed to equal `ECup - 1`; `CupGateHook` logs the
  ids it sees so this can be confirmed on first boot.
