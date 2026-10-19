"""Copy the mod to where the game runs.

Three targets, one layout under `<base>/<title id>/`:

  - `exefs/subsdk9`, `exefs/main.npdm`: the prebuilt mod
  - `romfs/ap_config.json`: `{"bridge_host": "<this PC's LAN address>"}`.
    LayeredFS (Atmosphere) / Ryujinx's mod loader add it to the game's
    RomFS; the mod reads it once when the game mounts its RomFS and sweeps
    that address's /24 to find the client (the dread_ap scheme). Nothing
    network-specific is compiled into the binary.

Targets:

  - ``ryujinx``: `<Ryujinx data dir>/mods/contents/0100152000022000/exefs/`.
    The data dir is `%APPDATA%/Ryujinx` unless Ryujinx runs portable (a
    `portable/` folder next to Ryujinx.exe), in which case the player
    browses to that folder.
  - ``sd``: `<SD root>/atmosphere/contents/0100152000022000/exefs/`. Drives
    that already hold an `atmosphere/` folder are offered automatically.
  - ``folder``: the SD layout under any folder, for players who move files
    to the SD with DBI / Goldleaf / FTP / a network share.

A file already at the destination that is not byte-identical to ours (another
MK8DX mod's subsdk9 or main.npdm) is kept once as `<name>.mk8ap-backup` and
restored by ``remove``.
"""

from __future__ import annotations

import json
import os
import shutil
import string
import sys
from dataclasses import dataclass, field
from pathlib import Path

from . import MOD_FILES, TITLE_ID
from .bundle import ModBundle, sha256_bytes

TARGETS = ("ryujinx", "sd", "folder")
TARGET_LABELS = {
    "ryujinx": "Ryujinx (emulator on this PC)",
    "sd": "Switch SD card (Atmosphere)",
    "folder": "Folder (copy to the SD card yourself)",
}
BACKUP_SUFFIX = ".mk8ap-backup"
CONFIG_NAME = "ap_config.json"


class DeployError(Exception):
    """A deploy could not complete; the message is shown to the player."""


@dataclass
class DeployResult:
    target: str
    exefs: Path
    copied: list[Path] = field(default_factory=list)
    unchanged: list[Path] = field(default_factory=list)
    backed_up: list[Path] = field(default_factory=list)
    bridge_host: str = ""

    def summary(self) -> str:
        parts = [f"Deployed to {self.exefs.parent}"]
        if self.copied:
            parts.append(f"wrote {', '.join(p.name for p in self.copied)}")
        if self.unchanged:
            parts.append(f"already current: {', '.join(p.name for p in self.unchanged)}")
        if self.backed_up:
            parts.append(f"kept the previous files as {', '.join(p.name for p in self.backed_up)}")
        parts.append(f"the Switch will look for this PC at {self.bridge_host} (its /24)")
        return "; ".join(parts)


# ---- detection -------------------------------------------------------------

def ryujinx_candidates() -> list[Path]:
    """Default Ryujinx data dirs for this platform, most likely first."""
    home = Path.home()
    if sys.platform == "win32":
        appdata = os.environ.get("APPDATA")
        return [Path(appdata) / "Ryujinx"] if appdata else []
    if sys.platform == "darwin":
        return [home / "Library" / "Application Support" / "Ryujinx"]
    xdg = os.environ.get("XDG_CONFIG_HOME")
    return [Path(xdg) / "Ryujinx" if xdg else home / ".config" / "Ryujinx",
            home / ".var" / "app" / "io.github.ryubing.Ryujinx" / "config" / "Ryujinx",
            home / ".var" / "app" / "org.ryujinx.Ryujinx" / "config" / "Ryujinx"]


def detect_ryujinx() -> Path | None:
    for p in ryujinx_candidates():
        if p.is_dir():
            return p
    return None


def normalize_ryujinx_root(path: Path) -> Path:
    """Accept the data dir itself, a portable install's exe folder (which
    holds `portable/`), or the `mods` folder, and return the data dir."""
    path = Path(path)
    if (path / "portable").is_dir():
        return path / "portable"
    if path.name.lower() == "mods":
        return path.parent
    return path


def detect_sd_cards() -> list[Path]:
    """Mounted drive roots that look like a Switch SD card (`atmosphere/`)."""
    if sys.platform != "win32":
        return []
    found = []
    for letter in string.ascii_uppercase[2:]:  # skip A: and B:
        root = Path(f"{letter}:/")
        try:
            if (root / "atmosphere").is_dir():
                found.append(root)
        except OSError:
            continue
    return found


# ---- the PC address written to ap_config.json -------------------------------

def detect_bridge_host() -> str:
    """This PC's LAN address, as the client advertises it to the Switch."""
    try:
        from ..client.net_util import detect_lan_ip  # inside the apworld
    except ImportError:
        from client.net_util import detect_lan_ip  # type: ignore[no-redef]  # source checkout / tests
    return detect_lan_ip()


def is_plausible_ipv4(s: str) -> bool:
    parts = s.split(".")
    return len(parts) == 4 and all(p.isdigit() and 0 <= int(p) <= 255 for p in parts)


def check_bridge_host(host: str) -> str:
    host = host.strip()
    if not is_plausible_ipv4(host):
        raise DeployError(f"'{host}' is not an IPv4 address like 192.168.1.20")
    if host.startswith("127.") or host == "0.0.0.0":
        raise DeployError(f"{host} is not reachable from a Switch; enter this PC's LAN address")
    return host


def read_config(target: str, root: Path) -> dict:
    try:
        data = json.loads((romfs_dir(target, root) / CONFIG_NAME).read_text(encoding="utf-8-sig"))
        return data if isinstance(data, dict) else {}
    except (OSError, ValueError, DeployError):
        return {}


# ---- layout ----------------------------------------------------------------

def exefs_dir(target: str, root: Path) -> Path:
    if target == "ryujinx":
        return normalize_ryujinx_root(root) / "mods" / "contents" / TITLE_ID / "exefs"
    if target in ("sd", "folder"):
        return Path(root) / "atmosphere" / "contents" / TITLE_ID / "exefs"
    raise DeployError(f"unknown deploy target {target!r} (expected one of {', '.join(TARGETS)})")


def romfs_dir(target: str, root: Path) -> Path:
    return exefs_dir(target, root).parent / "romfs"


def _check_root(target: str, root: Path | None) -> Path:
    if root is None or str(root).strip() in ("", "."):
        raise DeployError({"ryujinx": "Ryujinx's data folder was not found; browse to it "
                                      "(the folder that contains 'mods' and 'Config.json')",
                           "sd": "pick the SD card's drive",
                           "folder": "pick a folder"}.get(target, "pick a destination"))
    root = Path(root)
    if target == "ryujinx":
        data = normalize_ryujinx_root(root)
        if not data.is_dir():
            raise DeployError(f"{data} does not exist; start Ryujinx once, or browse to its data folder")
    elif target == "sd" and not root.exists():
        raise DeployError(f"{root} is not mounted; insert the SD card and try again")
    return root


# ---- deploy / status / remove ----------------------------------------------

def deploy(bundle: ModBundle, target: str, root: Path | None,
           bridge_host: str | None = None) -> DeployResult:
    """Copy the mod and write ap_config.json. `bridge_host` defaults to this
    PC's detected LAN address."""
    root = _check_root(target, root)
    host = check_bridge_host(bridge_host if bridge_host else detect_bridge_host())
    dest = exefs_dir(target, root)
    result = DeployResult(target=target, exefs=dest, bridge_host=host)
    try:
        dest.mkdir(parents=True, exist_ok=True)
    except OSError as e:
        raise DeployError(f"could not create {dest}: {e}") from e
    foreign = _foreign_install(dest)
    for name in MOD_FILES:
        data = bundle.files[name]
        out = dest / name
        try:
            if out.is_file():
                if out.read_bytes() == data:
                    result.unchanged.append(out)
                    continue
                backup = out.with_name(name + BACKUP_SUFFIX)
                if foreign and not backup.exists():
                    shutil.copy2(out, backup)
                    result.backed_up.append(backup)
            tmp = out.with_name(name + ".mk8ap-tmp")
            tmp.write_bytes(data)
            os.replace(tmp, out)
        except OSError as e:
            raise DeployError(f"could not write {out}: {e}. If this is an SD card, check it is "
                              "inserted and not write-protected; if Ryujinx, close the game first.") from e
        if sha256_bytes(out.read_bytes()) != bundle.sha256(name):
            raise DeployError(f"{out} does not match after copying (drive full or removed?)")
        result.copied.append(out)
    _write_config(romfs_dir(target, root) / CONFIG_NAME, host)
    return result


def _write_config(path: Path, host: str) -> None:
    data = json.dumps({"bridge_host": host}, separators=(",", ":")).encode("utf-8")
    try:
        path.parent.mkdir(parents=True, exist_ok=True)
        tmp = path.with_name(path.name + ".mk8ap-tmp")
        tmp.write_bytes(data)
        os.replace(tmp, path)
    except OSError as e:
        raise DeployError(f"could not write {path}: {e}") from e
    if path.read_bytes() != data:
        raise DeployError(f"{path} does not match after writing (drive full or removed?)")


def _is_ours(subsdk9: bytes) -> bool:
    # The NSO's (uncompressed) module header carries MODULE_NAME. main.npdm
    # is generic, so ownership of a directory is decided by its subsdk9.
    return b"mk8dx_archipelago" in subsdk9


def _foreign_install(dest: Path) -> bool:
    """True when `dest` holds mod files that are not ours: a subsdk9 from
    another mod, or a main.npdm with no subsdk9 beside it."""
    sub = dest / "subsdk9"
    if sub.is_file():
        return not _is_ours(sub.read_bytes())
    return (dest / "main.npdm").is_file()


@dataclass
class TargetStatus:
    exefs: Path
    state: str  # "missing" | "current" | "different"
    detail: str = ""
    bridge_host: str = ""  # from ap_config.json, if present


def status(bundle: ModBundle | None, target: str, root: Path | None) -> TargetStatus:
    """Is the bundled mod what is deployed at this target?"""
    try:
        dest = exefs_dir(target, _check_root(target, root))
    except DeployError as e:
        return TargetStatus(exefs=Path(str(root or "")), state="missing", detail=str(e))
    present = {n: dest / n for n in MOD_FILES if (dest / n).is_file()}
    if not present:
        return TargetStatus(exefs=dest, state="missing", detail="not installed")
    if bundle is None:
        return TargetStatus(exefs=dest, state="different", detail="installed (no bundled mod to compare)")
    stale = [n for n in MOD_FILES
             if n not in present or sha256_bytes(present[n].read_bytes()) != bundle.sha256(n)]
    host = str(read_config(target, root).get("bridge_host", ""))
    if stale:
        return TargetStatus(exefs=dest, state="different", bridge_host=host,
                            detail=f"{' and '.join(stale)} {'differs' if len(stale) == 1 else 'differ'} "
                                   f"from the bundled mod {bundle.version}")
    if not host:
        return TargetStatus(exefs=dest, state="different",
                            detail=f"mod {bundle.version} installed but ap_config.json is missing")
    return TargetStatus(exefs=dest, state="current", bridge_host=host,
                        detail=f"mod {bundle.version} installed, PC address {host}")


def remove(target: str, root: Path | None) -> list[str]:
    """Delete our files at the target and restore any backups. Returns lines
    describing what happened."""
    root = _check_root(target, root)
    dest = exefs_dir(target, root)
    if _foreign_install(dest):
        return [f"left {dest} alone: the mod there is not MK8DX Archipelago"]
    lines = []
    for name in MOD_FILES:
        out = dest / name
        backup = out.with_name(name + BACKUP_SUFFIX)
        try:
            if backup.is_file():
                os.replace(backup, out)
                lines.append(f"restored the previous {name}")
            elif out.is_file():
                out.unlink()
                lines.append(f"removed {out}")
        except OSError as e:
            raise DeployError(f"could not remove {out}: {e}") from e
    cfg = romfs_dir(target, root) / CONFIG_NAME
    if cfg.is_file():
        try:
            cfg.unlink()
            lines.append(f"removed {cfg}")
        except OSError as e:
            raise DeployError(f"could not remove {cfg}: {e}") from e
    for d in (dest, cfg.parent):
        try:
            if d.is_dir() and not any(d.iterdir()):
                d.rmdir()
        except OSError:
            pass
    return lines or [f"nothing to remove at {dest}"]
