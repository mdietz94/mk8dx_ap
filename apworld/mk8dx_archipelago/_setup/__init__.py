"""Setup for the MK8DX Archipelago Switch mod.

The release `mk8dx.apworld` carries a PREBUILT mod (`_setup/mod/subsdk9`,
`main.npdm`, `manifest.json`). One binary serves every player because
nothing about the player's network is compiled in: deploy writes
`romfs/ap_config.json` = {"bridge_host": <this PC's LAN address>} next to
the mod, and the mod sweeps that address's /24 (the dread_ap scheme). So
setup is only: pick where the game runs (Ryujinx, a Switch SD card, or a
folder you copy to the SD yourself), confirm the PC address, verify the
bundled files against the manifest, copy them, and optionally open the
Windows Firewall ports. No toolchain, no build, no game data extraction.

Modules (each importable without Kivy or Archipelago):

  - `bundle.py`  — read + verify the bundled mod (works inside the zip)
  - `deploy.py`  — deploy targets, copy with backup, status, removal
  - `state.py`   — `setup_state.json` (last target, deployed version)
  - `firewall.py`— inbound TCP 17778 / UDP 17779 rules (Windows)
  - `cli.py`     — headless front end (tests, dev loop, no-GUI players)
  - `wizard.py`  — Kivy front end, opened by `/setup` in the MK8DX Client
                   and automatically on the client's first run
"""

from __future__ import annotations

import os
import sys
from pathlib import Path

TITLE_ID = "0100152000022000"
GAME_VERSION = "4.0.1"
# Files the mod consists of, in deploy order.
MOD_FILES = ("subsdk9", "main.npdm")


def appdata_root() -> Path:
    """Per-user state dir: `%APPDATA%/MK8DXArchipelago/` on Windows,
    `~/.local/share/MK8DXArchipelago/` elsewhere. `MK8AP_APPDATA_ROOT`
    overrides it (tests and the release audit; the wizard never sets it)."""
    override = os.environ.get("MK8AP_APPDATA_ROOT")
    if override:
        root = Path(override)
    elif sys.platform == "win32" and os.environ.get("APPDATA"):
        root = Path(os.environ["APPDATA"]) / "MK8DXArchipelago"
    else:
        root = Path.home() / ".local" / "share" / "MK8DXArchipelago"
    root.mkdir(parents=True, exist_ok=True)
    return root


def setup_state_path() -> Path:
    return appdata_root() / "setup_state.json"


def wizard_log_path() -> Path:
    return appdata_root() / "wizard.log"
