"""Audit a built mk8dx.apworld before it is published.

    python scripts/release_audit.py dist/mk8dx.apworld            # bundled mod required
    python scripts/release_audit.py dist/mk8dx.apworld --no-mod   # dev zip

Fails (exit 1) on any of:

  * IP discipline (Nintendo intellectual property): an entry outside the
    allowlist below, or anything that looks like a dump, key, RomFS asset or
    extracted game data (.nso/.nsp/.xci/.nca/.keys/.szs/.bfres/.byml/.msbt/
    .sarc/.bntx/...). The only binaries allowed are the mod's own subsdk9 and
    main.npdm, built from this repository.
  * Bundled mod integrity: manifest missing, SHA-256/size mismatch, subsdk9
    not an NSO carrying our module name, main.npdm not for title
    0100152000022000.
  * Privacy / portability: the decompressed mod contains a dotted IPv4
    literal other than 127.0.0.1 / 0.0.0.0 (a hand-patched or old
    -DBRIDGE_HOST build: the release binary must be network-agnostic; the
    sweep seed belongs in the deploy-time ap_config.json), or an absolute
    home-directory path (C:/Users/<name>, /home/<name>)
    in the mod or any text file.
  * Version drift (with the mod): manifest mod_version, archipelago.json
    world_version and client/__init__.py __version__ must agree.

Prints the SHA-256 of the zip on success.
"""

from __future__ import annotations

import argparse
import fnmatch
import hashlib
import json
import re
import struct
import sys
import zipfile
from pathlib import PurePosixPath

STEM = "mk8dx"
TITLE_ID = 0x0100152000022000
MODULE_NAME = b"mk8dx_archipelago"

# Every shipped path must match one of these (relative to mk8dx/).
ALLOWED_GLOBS = (
    "*.py",
    "archipelago.json",
    "data/*.json",
    "docs/*.md",
    "_setup/mod/subsdk9",
    "_setup/mod/main.npdm",
    "_setup/mod/manifest.json",
)
FORBIDDEN_SUFFIXES = {
    ".nso", ".nro", ".nrr", ".nsp", ".xci", ".nca", ".nsz", ".xcz", ".tik", ".cert", ".keys",
    ".szs", ".sarc", ".bfres", ".byml", ".bymlb", ".msbt", ".msbp", ".bntx", ".bflyt", ".bflan",
    ".bfsar", ".bars", ".bwav", ".kcl", ".bin", ".pyc", ".log",
}
FORBIDDEN_NAMES = {"prod.keys", "title.keys", "dev.keys", "mk8dx_learned_ids.json", "main", "rtld", "sdk"}
IPV4_RE = re.compile(rb"(?<![\d.])(\d{1,3})\.(\d{1,3})\.(\d{1,3})\.(\d{1,3})(?![\d.])")
ALLOWED_IPV4 = {b"127.0.0.1", b"0.0.0.0"}
HOME_PATH_RE = re.compile(rb"(?i)(?:[a-z]:[\\/]+users[\\/]+[^\\/\s\"']+|/home/[^/\s\"']+|/Users/[^/\s\"']+)")
# Placeholder home paths in docs/comments are fine.
HOME_PATH_OK = re.compile(rb"(?i)(users[\\/]+(you|<[^>]*>|name|user|username|\.\.\.)|home/(you|user|runner))")


def lz4_block_decompress(src: bytes, out_size: int) -> bytes:
    """Decode one raw LZ4 block (the format NSO segments use)."""
    out = bytearray()
    i, n = 0, len(src)
    while i < n:
        token = src[i]
        i += 1
        lit = token >> 4
        if lit == 15:
            while True:
                b = src[i]
                i += 1
                lit += b
                if b != 255:
                    break
        out += src[i:i + lit]
        i += lit
        if i >= n:
            break
        offset = src[i] | (src[i + 1] << 8)
        i += 2
        if offset == 0 or offset > len(out):
            raise ValueError("corrupt LZ4 block")
        match = (token & 15) + 4
        if (token & 15) == 15:
            while True:
                b = src[i]
                i += 1
                match += b
                if b != 255:
                    break
        start = len(out) - offset
        for k in range(match):  # may overlap: byte by byte
            out.append(out[start + k])
    if len(out) != out_size:
        raise ValueError(f"LZ4 block decoded to {len(out)} bytes, header says {out_size}")
    return bytes(out)


def nso_segments(nso: bytes) -> list[bytes]:
    """Decompressed .text, .rodata, .data of an NSO."""
    if nso[:4] != b"NSO0":
        raise ValueError("not an NSO (bad magic)")
    flags = struct.unpack_from("<I", nso, 0x0C)[0]
    segs = []
    for idx, hdr in enumerate((0x10, 0x20, 0x30)):
        file_off, _mem_off, size = struct.unpack_from("<III", nso, hdr)
        file_size = struct.unpack_from("<I", nso, 0x60 + 4 * idx)[0]
        raw = nso[file_off:file_off + file_size]
        segs.append(lz4_block_decompress(raw, size) if flags & (1 << idx) else raw[:size])
    return segs


class Audit:
    def __init__(self) -> None:
        self.errors: list[str] = []

    def fail(self, msg: str) -> None:
        self.errors.append(msg)

    def scan_home_paths(self, where: str, data: bytes) -> None:
        for m in HOME_PATH_RE.finditer(data):
            if not HOME_PATH_OK.search(m.group(0)):
                self.fail(f"{where}: absolute home path {m.group(0)[:60]!r}")
                return


def audit_zip(path: str, *, require_mod: bool) -> Audit:
    a = Audit()
    try:
        zf = zipfile.ZipFile(path)
    except (OSError, zipfile.BadZipFile) as e:
        a.fail(f"cannot open {path}: {e}")
        return a
    names = [n for n in zf.namelist() if not n.endswith("/")]
    rel: dict[str, str] = {}
    for n in names:
        p = PurePosixPath(n)
        if p.parts[0] != STEM or ".." in p.parts:
            a.fail(f"{n}: outside {STEM}/")
            continue
        r = "/".join(p.parts[1:])
        rel[r] = n
        low = p.name.lower()
        if p.suffix.lower() in FORBIDDEN_SUFFIXES or low in FORBIDDEN_NAMES:
            a.fail(f"{n}: forbidden file type (dump / key / game asset / build droppings)")
        elif r.startswith("tests/") or "__pycache__" in p.parts:
            a.fail(f"{n}: test or cache file in the release")
        elif not any(fnmatch.fnmatchcase(r, g) for g in ALLOWED_GLOBS):
            a.fail(f"{n}: not in the release allowlist (scripts/release_audit.py ALLOWED_GLOBS)")
        if r.endswith((".py", ".json", ".md")):
            a.scan_home_paths(n, zf.read(n))

    for required in ("__init__.py", "archipelago.json", "client/main.py", "_setup/wizard.py",
                     "_setup/deploy.py", "data/items.json", "data/locations.json"):
        if required not in rel:
            a.fail(f"{STEM}/{required} missing")

    mod = {k: rel.get(f"_setup/mod/{k}") for k in ("subsdk9", "main.npdm", "manifest.json")}
    if not any(mod.values()):
        if require_mod:
            a.fail("no bundled mod (_setup/mod/); build with scripts/build_release.py "
                   "or pass --no-mod for a dev zip")
        return a
    if not all(mod.values()):
        a.fail(f"incomplete bundled mod: {', '.join(k for k, v in mod.items() if not v)} missing")
        return a

    manifest = json.loads(zf.read(mod["manifest.json"]))
    for name in ("subsdk9", "main.npdm"):
        data = zf.read(mod[name])
        meta = manifest.get("files", {}).get(name, {})
        if meta.get("sha256") != hashlib.sha256(data).hexdigest() or meta.get("size") != len(data):
            a.fail(f"_setup/mod/{name}: does not match manifest.json")

    sub = zf.read(mod["subsdk9"])
    try:
        segs = nso_segments(sub)
    except (ValueError, struct.error, IndexError) as e:
        a.fail(f"_setup/mod/subsdk9: {e}")
        segs = []
    blob = b"".join(segs)
    if segs and MODULE_NAME not in blob and MODULE_NAME not in sub:
        a.fail("_setup/mod/subsdk9: module name mk8dx_archipelago not found (not our build?)")
    for m in IPV4_RE.finditer(blob):
        lit = m.group(0)
        if lit not in ALLOWED_IPV4 and all(int(g) <= 255 for g in m.groups()):
            a.fail(f"_setup/mod/subsdk9: IPv4 literal {lit.decode()} baked in "
                   "(the release binary must be network-agnostic; the seed belongs in ap_config.json)")
    a.scan_home_paths("_setup/mod/subsdk9", blob)

    npdm = zf.read(mod["main.npdm"])
    if npdm[:4] != b"META" or struct.pack("<Q", TITLE_ID) not in npdm:
        a.fail("_setup/mod/main.npdm: not a META for title 0100152000022000")

    world_ver = json.loads(zf.read(rel["archipelago.json"])).get("world_version") \
        if "archipelago.json" in rel else None
    client_init = zf.read(rel["client/__init__.py"]).decode() if "client/__init__.py" in rel else ""
    m = re.search(r'__version__\s*=\s*"([^"]+)"', client_init)
    client_ver = m.group(1) if m else None
    versions = {"manifest mod_version": manifest.get("mod_version"),
                "archipelago.json world_version": world_ver,
                "client __version__": client_ver}
    if len(set(versions.values())) != 1:
        a.fail("version drift: " + ", ".join(f"{k}={v}" for k, v in versions.items()))
    return a


def main(argv: list[str] | None = None) -> int:
    p = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    p.add_argument("apworld")
    p.add_argument("--no-mod", action="store_true", help="the zip is not expected to bundle the mod")
    args = p.parse_args(argv)
    a = audit_zip(args.apworld, require_mod=not args.no_mod)
    if a.errors:
        print(f"FAIL: {len(a.errors)} problem(s) in {args.apworld}:", file=sys.stderr)
        for e in a.errors:
            print(f"  - {e}", file=sys.stderr)
        return 1
    digest = hashlib.sha256(open(args.apworld, "rb").read()).hexdigest()
    print(f"OK: {args.apworld} passed the release audit; sha256 {digest}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
