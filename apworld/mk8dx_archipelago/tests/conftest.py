"""Test path setup: makes `client` importable as a top-level package
(`from client.X import Y`) by putting apworld/mk8dx_archipelago/ on sys.path.

vendor/Archipelago is NOT added here (importing `worlds` triggers apworld
discovery and pulls unrelated deps). Tests that need CommonClient add it
themselves and skip when it is absent.
"""

from __future__ import annotations

import sys
from pathlib import Path

_APWORLD_ROOT = Path(__file__).resolve().parent.parent
if str(_APWORLD_ROOT) not in sys.path:
    sys.path.insert(0, str(_APWORLD_ROOT))
