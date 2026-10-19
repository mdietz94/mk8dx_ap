# Release process

## What ships

One file, `mk8dx.apworld`, plus `mk8dx.apworld.sha256`. Inside the zip:

| Path | Source |
|---|---|
| `mk8dx/...` | `apworld/mk8dx_archipelago/` minus `tests/`, caches, logs, learned-id files |
| `mk8dx/_setup/mod/subsdk9`, `main.npdm` | the Switch mod, built by `scripts/build_switchmod.py` |
| `mk8dx/_setup/mod/manifest.json` | mod version (= `world_version`), title id, git commit, SHA-256 + size of each file |

The mod is prebuilt because nothing in it is player-specific: the PC address
it sweeps comes from `romfs/ap_config.json`, written at deploy time. It
contains only this
repository's code, LibHakkun, and a few dozen bytes per game function as
search patterns (the same kind of data every public MK8DX patch carries). No
Nintendo code, assets or keys.

## Cut a release

1. Bump the version in **both** `apworld/mk8dx_archipelago/archipelago.json`
   (`world_version`) and `apworld/mk8dx_archipelago/client/__init__.py`
   (`__version__`). The audit fails if they differ.
2. Merge to `main`.
3. Tag and push:

   ```
   git tag -a v0.2.0 -m v0.2.0
   git push origin v0.2.0
   ```

`.github/workflows/release.yml` then builds the mod on Ubuntu (LLVM 19 from
apt.llvm.org), runs `scripts/build_release.py`, and publishes a GitHub
Release with both files and `docs/release-notes-template.md` as the body.
Tags containing `-` (`v0.2.0-rc1`) become pre-releases. The workflow also
runs, without publishing, on pull requests that touch the mod or packaging,
and on manual dispatch (download the `mk8dx-apworld` artifact to inspect).

## Build locally

```
python scripts/build_release.py            # build mod + zip + audit + sha256 -> dist/
python scripts/build_release.py --publish  # same, then gh release create (tag must point at HEAD)
```

Windows needs the toolchain from the README (LLVM 19, CMake, Ninja, msys2
mingw64 g++). `--skip-build` re-bundles `switch-mod/build/exefs` without
rebuilding.

## The audit (`scripts/release_audit.py`)

Runs on every release build; run it by hand on any zip:

```
python scripts/release_audit.py dist/mk8dx.apworld
```

It fails on:

- **IP discipline:** any entry outside the allowlist (`*.py`,
  `archipelago.json`, `data/*.json`, `docs/*.md`, the three `_setup/mod/`
  files), or anything that looks like a dump, key, RomFS asset or extracted
  data (`.nso .nsp .xci .nca .keys .szs .bfres .byml .msbt .sarc .bntx ...`),
  tests, caches, logs, `mk8dx_learned_ids.json`.
- **Integrity:** manifest missing or not matching the files, `subsdk9` not an
  NSO with module name `mk8dx_archipelago`, `main.npdm` not for title
  `0100152000022000`.
- **Network-agnostic build:** the NSO's segments are LZ4-decompressed and
  scanned; any dotted IPv4 literal other than `127.0.0.1` / `0.0.0.0` means
  something network-specific was compiled in.
- **Privacy:** an absolute home path (`C:\Users\<name>`, `/home/<name>`) in
  the mod or in any shipped text file.
- **Version drift:** manifest `mod_version`, `world_version`, `__version__`.

Extend `ALLOWED_GLOBS` deliberately when a new kind of file must ship.

## After publishing

Install the released zip in a clean Archipelago (`custom_worlds`), open MK8DX
Client: the wizard should open, show the new version as verified, and install
to Ryujinx. Boot the game and check the client logs `switch HELLO mod=<new
version>`.
