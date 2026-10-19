"""Zip apworld/mk8dx_archipelago/ into vendor/Archipelago/custom_worlds/mk8dx.apworld.

An `.apworld` is a zip with the world package at its root. Archipelago
auto-discovers `.apworld` files in `<checkout>/custom_worlds/` at startup.
The zip is named `mk8dx.apworld`, so Archipelago imports the world as
`worlds.mk8dx` and the host.yaml settings key is `mk8dx_options`.

    python scripts/install_apworld.py                 # apworld only (dev / tests)
    python scripts/install_apworld.py --bundle-mod    # + the built mod under _setup/mod/
    python scripts/install_apworld.py --bundle-mod --output dist/mk8dx.apworld

`--bundle-mod` adds `mk8dx/_setup/mod/{subsdk9,main.npdm,manifest.json}`
from `switch-mod/build/exefs/` (or `--mod-dir`). The manifest carries the
mod version and each file's SHA-256; the setup wizard verifies both before
deploying. Run scripts/release_audit.py on the result (scripts/build_release.py does all of
this in one go).
"""

from __future__ import annotations

import argparse
import json
import subprocess
import sys
import zipfile
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
SRC = REPO / "apworld" / "mk8dx_archipelago"
DST_DIR = REPO / "vendor" / "Archipelago" / "custom_worlds"
DST = DST_DIR / "mk8dx.apworld"
ZIP_STEM = "mk8dx"
MOD_DIR = REPO / "switch-mod" / "build" / "exefs"
MOD_FILES = ("subsdk9", "main.npdm")

SKIP_NAMES = {"__pycache__", ".mypy_cache", ".ruff_cache", ".pytest_cache", "tests", "logs"}
# Per-machine runtime files that must never ship (learned ids come from the
# player's own game; logs can hold LAN addresses).
SKIP_SUFFIXES = {".pyc", ".pyo", ".log"}
SKIP_FILES = {"mk8dx_learned_ids.json"}


def parse_args(argv: list[str] | None = None) -> argparse.Namespace:
    p = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    p.add_argument("--bundle-mod", action="store_true",
                   help="also bundle the built mod (subsdk9, main.npdm + manifest) under _setup/mod/")
    p.add_argument("--mod-dir", type=Path, default=MOD_DIR,
                   help=f"where the built mod is (default {MOD_DIR.relative_to(REPO)})")
    p.add_argument("--mod-version", default=None,
                   help="version recorded in the manifest (default: archipelago.json world_version)")
    p.add_argument("--output", type=Path, default=None, help=f"zip path (default {DST.relative_to(REPO)})")
    return p.parse_args(argv)


def world_version() -> str:
    return json.loads((SRC / "archipelago.json").read_text(encoding="utf-8"))["world_version"]


def git_commit() -> str:
    try:
        return subprocess.run(["git", "rev-parse", "HEAD"], cwd=REPO, capture_output=True,
                              text=True, check=True).stdout.strip()
    except (OSError, subprocess.CalledProcessError):
        return ""


def _collect(root: Path) -> list[Path]:
    return sorted(p for p in root.rglob("*")
                  if p.is_file()
                  and not any(part in SKIP_NAMES for part in p.relative_to(root).parts)
                  and p.suffix not in SKIP_SUFFIXES and p.name not in SKIP_FILES)


def build_manifest(mod_dir: Path, mod_version: str) -> tuple[dict, dict[str, bytes]]:
    sys.path.insert(0, str(SRC))
    try:
        from _setup.bundle import make_manifest  # type: ignore[import-not-found]
    finally:
        sys.path.remove(str(SRC))
    files = {name: (mod_dir / name).read_bytes() for name in MOD_FILES}
    return make_manifest(files, mod_version=mod_version, git_commit=git_commit(),
                         built_by="scripts/install_apworld.py"), files


def main(argv: list[str] | None = None) -> int:
    args = parse_args(argv)
    dst = args.output or DST
    if not SRC.is_dir():
        print(f"FAIL: apworld source not found at {SRC}", file=sys.stderr)
        return 2
    if args.output is None and not DST_DIR.parent.is_dir():
        print(f"FAIL: Archipelago checkout not found at {DST_DIR.parent} "
              "(run `git submodule update --init vendor/Archipelago`, or pass --output)", file=sys.stderr)
        return 2
    dst.parent.mkdir(parents=True, exist_ok=True)
    files = _collect(SRC)
    if not files:
        print(f"FAIL: no files under {SRC}", file=sys.stderr)
        return 2
    extra: dict[str, bytes] = {}
    if args.bundle_mod:
        missing = [n for n in MOD_FILES if not (args.mod_dir / n).is_file()]
        if missing:
            print(f"FAIL: --bundle-mod but {', '.join(missing)} missing under {args.mod_dir} "
                  "(build the mod first: python scripts/build_switchmod.py)", file=sys.stderr)
            return 2
        manifest, mod = build_manifest(args.mod_dir, args.mod_version or world_version())
        base = f"{ZIP_STEM}/_setup/mod"
        for name, data in mod.items():
            extra[f"{base}/{name}"] = data
        extra[f"{base}/manifest.json"] = json.dumps(manifest, indent=2).encode("utf-8")
    with zipfile.ZipFile(dst, "w", compression=zipfile.ZIP_DEFLATED) as zf:
        for p in files:
            zf.write(p, (Path(ZIP_STEM) / p.relative_to(SRC)).as_posix())
        for arc, data in extra.items():
            zf.writestr(arc, data)
    note = f", mod {json.loads(extra[f'{ZIP_STEM}/_setup/mod/manifest.json'])['mod_version']}" if extra else ""
    print(f"OK: wrote {dst} ({len(files) + len(extra)} files, {dst.stat().st_size / 1024:.1f} KiB{note})")
    return 0


if __name__ == "__main__":
    sys.exit(main())
