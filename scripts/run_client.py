"""Run the MK8DX Client without going through the Archipelago Launcher.

    .venv/Scripts/python scripts/run_client.py --nogui --connect localhost:38281 --name Mario

Puts vendor/Archipelago on sys.path (so CommonClient / worlds import),
suppresses Archipelago's auto-pip, imports the installed apworld
(vendor/Archipelago/custom_worlds/mk8dx.apworld, see install_apworld.py)
and calls its client `launch`. Any extra args go to the client parser.
"""

from __future__ import annotations

import os
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
AP_ROOT = REPO / "vendor" / "Archipelago"

os.chdir(AP_ROOT)
sys.path.insert(0, str(AP_ROOT))

import ModuleUpdate  # type: ignore[import-not-found]  # noqa: E402
ModuleUpdate.update_ran = True

import worlds  # noqa: E402,F401  (registers custom_worlds/*.apworld)
from worlds.mk8dx.client.main import launch  # type: ignore[import-not-found]  # noqa: E402

if __name__ == "__main__":
    launch(*sys.argv[1:])
