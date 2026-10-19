"""The prebuilt Switch mod bundled in the apworld.

`scripts/install_apworld.py --bundle-mod` writes three entries under
`mk8dx/_setup/mod/`: `subsdk9`, `main.npdm` and `manifest.json`. The
manifest records the mod version (equal to the apworld's world_version on a
release) and each file's SHA-256 and size, so a truncated download or a
hand-edited zip is caught before anything reaches an SD card.

Read through `importlib.resources`, which works both from a source checkout
and from inside the `.apworld` zip (zipimport).
"""

from __future__ import annotations

import hashlib
import json
from dataclasses import dataclass, field
from importlib import resources
from pathlib import Path

from . import MOD_FILES

MANIFEST_NAME = "manifest.json"
MOD_SUBDIR = "mod"


class BundleError(Exception):
    """The bundled mod is missing, incomplete or fails verification."""


@dataclass
class ModBundle:
    """The mod files plus their manifest, verified."""
    manifest: dict
    files: dict[str, bytes] = field(default_factory=dict)
    source: str = ""  # where it was read from, for messages

    @property
    def version(self) -> str:
        return str(self.manifest.get("mod_version", "?"))

    def sha256(self, name: str) -> str:
        return str(self.manifest["files"][name]["sha256"])


def sha256_bytes(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def make_manifest(files: dict[str, bytes], *, mod_version: str, git_commit: str = "",
                  built_by: str = "") -> dict:
    """The manifest `install_apworld.py --bundle-mod` writes."""
    from . import GAME_VERSION, TITLE_ID
    return {
        "mod_version": mod_version,
        "title_id": TITLE_ID,
        "game_version": GAME_VERSION,
        "git_commit": git_commit,
        "built_by": built_by,
        "files": {name: {"sha256": sha256_bytes(data), "size": len(data)}
                  for name, data in files.items()},
    }


def verify(manifest: dict, files: dict[str, bytes], source: str = "") -> ModBundle:
    """Check every MOD_FILES entry against the manifest; raise BundleError."""
    listed = manifest.get("files")
    if not isinstance(listed, dict):
        raise BundleError(f"{source}: manifest has no 'files' table")
    for name in MOD_FILES:
        if name not in files:
            raise BundleError(f"{source}: {name} is missing")
        meta = listed.get(name)
        if not isinstance(meta, dict) or "sha256" not in meta:
            raise BundleError(f"{source}: manifest does not list {name}")
        data = files[name]
        if len(data) != int(meta.get("size", len(data))):
            raise BundleError(f"{source}: {name} is {len(data)} bytes, manifest says {meta['size']}"
                              " (truncated download?)")
        if sha256_bytes(data) != meta["sha256"]:
            raise BundleError(f"{source}: {name} does not match its manifest SHA-256 "
                              "(corrupt or modified apworld)")
    return ModBundle(manifest=manifest, files=dict(files), source=source)


def _bundled_dir():
    return resources.files(__package__).joinpath(MOD_SUBDIR)


def has_bundled_mod() -> bool:
    try:
        return _bundled_dir().joinpath(MANIFEST_NAME).is_file()
    except (FileNotFoundError, ModuleNotFoundError, NotADirectoryError):
        return False


def load_bundled() -> ModBundle:
    """The mod shipped in this apworld. Raises BundleError when absent (a
    source checkout or a dev zip built without --bundle-mod)."""
    if not has_bundled_mod():
        raise BundleError("this apworld has no bundled Switch mod (built without --bundle-mod); "
                          "download the release mk8dx.apworld, or deploy a local build with "
                          "--from-dir")
    d = _bundled_dir()
    try:
        manifest = json.loads(d.joinpath(MANIFEST_NAME).read_text(encoding="utf-8"))
        files = {name: d.joinpath(name).read_bytes() for name in MOD_FILES
                 if d.joinpath(name).is_file()}
    except (OSError, ValueError) as e:
        raise BundleError(f"bundled mod unreadable: {e}") from e
    return verify(manifest, files, source="bundled mod")


def load_from_dir(path: Path) -> ModBundle:
    """A local build (`switch-mod/build/exefs/`). No manifest is required:
    one is synthesised so the deploy path is identical."""
    files = {}
    for name in MOD_FILES:
        p = path / name
        if not p.is_file():
            raise BundleError(f"{p} not found (build the mod first: python scripts/build_switchmod.py)")
        files[name] = p.read_bytes()
    manifest_path = path / MANIFEST_NAME
    if manifest_path.is_file():
        manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    else:
        manifest = make_manifest(files, mod_version="local", built_by=str(path))
    return verify(manifest, files, source=str(path))
