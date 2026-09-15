"""Zip apworld/mk8dx_archipelago/ into vendor/Archipelago/custom_worlds/mk8dx.apworld.

An `.apworld` is a zip with the world package at its root. Archipelago
auto-discovers `.apworld` files in `<checkout>/custom_worlds/` at startup.
The zip is named `mk8dx.apworld`, so Archipelago imports the world as
`worlds.mk8dx` and the host.yaml settings key is `mk8dx_options`.

    python scripts/install_apworld.py                 # apworld only (dev / tests)
    python scripts/install_apworld.py --bundle-mod    # + prebuilt subsdk9 under _setup/
"""

from __future__ import annotations

import argparse
import sys
import zipfile
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
SRC = REPO / "apworld" / "mk8dx_archipelago"
DST_DIR = REPO / "vendor" / "Archipelago" / "custom_worlds"
DST = DST_DIR / "mk8dx.apworld"
ZIP_STEM = "mk8dx"

SKIP_NAMES = {"__pycache__", ".mypy_cache", ".ruff_cache", ".pytest_cache", "tests"}


def parse_args(argv: list[str] | None = None) -> argparse.Namespace:
    p = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    p.add_argument("--bundle-mod", action="store_true",
                   help="also copy switch-mod/build/exefs/{subsdk9,main.npdm} under _setup/exefs/")
    return p.parse_args(argv)


def _collect(root: Path) -> list[Path]:
    return [p for p in root.rglob("*")
            if p.is_file() and not any(part in SKIP_NAMES for part in p.parts)]


def main(argv: list[str] | None = None) -> int:
    args = parse_args(argv)
    if not SRC.is_dir():
        print(f"FAIL: apworld source not found at {SRC}", file=sys.stderr)
        return 2
    if not DST_DIR.parent.is_dir():
        print(f"FAIL: Archipelago checkout not found at {DST_DIR.parent} "
              "(run `git submodule update --init vendor/Archipelago`)", file=sys.stderr)
        return 2
    DST_DIR.mkdir(parents=True, exist_ok=True)
    files = _collect(SRC)
    if not files:
        print(f"FAIL: no files under {SRC}", file=sys.stderr)
        return 2
    extra: list[tuple[Path, Path]] = []
    if args.bundle_mod:
        exefs = REPO / "switch-mod" / "build" / "exefs"
        for name in ("subsdk9", "main.npdm"):
            p = exefs / name
            if not p.exists():
                print(f"FAIL: --bundle-mod but {p} missing (build the mod first)", file=sys.stderr)
                return 2
            extra.append((p, Path(ZIP_STEM) / "_setup" / "exefs" / name))
    with zipfile.ZipFile(DST, "w", compression=zipfile.ZIP_DEFLATED) as zf:
        for p in files:
            zf.write(p, (Path(ZIP_STEM) / p.relative_to(SRC)).as_posix())
        for src, arc in extra:
            zf.write(src, arc.as_posix())
    print(f"OK: wrote {DST} ({len(files) + len(extra)} files, {DST.stat().st_size / 1024:.1f} KiB)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
