"""Apworld generation sweep (opt-in: MK8AP_LIVE_AP=1).

Drives scripts/ap_generate.py over a few option combinations. Needs the
vendor/Archipelago submodule with its pip deps installed.

    MK8AP_LIVE_AP=1 python -m pytest -v apworld/mk8dx_archipelago/tests/test_apworld_generation.py
"""

from __future__ import annotations

import os
import subprocess
import sys
import tempfile
from pathlib import Path

import pytest

REPO = Path(__file__).resolve().parents[3]
GEN_SCRIPT = REPO / "scripts" / "ap_generate.py"
INSTALL_SCRIPT = REPO / "scripts" / "install_apworld.py"

pytestmark = pytest.mark.skipif(os.environ.get("MK8AP_LIVE_AP") != "1",
                                reason="set MK8AP_LIVE_AP=1 to run generation tests")


@pytest.fixture(scope="module")
def _apworld_installed():
    r = subprocess.run([sys.executable, str(INSTALL_SCRIPT)], capture_output=True, text=True)
    if r.returncode != 0:
        pytest.fail(f"install_apworld.py failed:\n{r.stdout}\n{r.stderr}")


def _yaml(overrides: dict) -> str:
    base = {"accessibility": "minimal"}
    base.update(overrides)
    body = "\n".join(f"  {k}: {str(v).lower() if isinstance(v, bool) else v}" for k, v in base.items())
    return f"name: Mario\ngame: Mario Kart 8 Deluxe\n\nMario Kart 8 Deluxe:\n{body}\n"


def _generate(yaml_dir: Path) -> subprocess.CompletedProcess:
    out = yaml_dir / "out"
    out.mkdir(exist_ok=True)
    return subprocess.run([sys.executable, str(GEN_SCRIPT), "--player_files_path", str(yaml_dir),
                           "--outputpath", str(out), "--skip_output", "--seed", "20260915"],
                          capture_output=True, text=True, stdin=subprocess.DEVNULL)


SCENARIOS = [
    ("defaults", {}),
    ("base_game", {"course_selection": "base_game"}),
    ("bcp_only", {"course_selection": "booster_course_pass"}),
    ("bcp_only_finish_only", {"course_selection": "booster_course_pass",
                              "include_top3_locations": False, "include_win_locations": False}),
    ("finish_only", {"include_top3_locations": False, "include_win_locations": False}),
    ("no_shuffles", {"shuffle_characters": False, "shuffle_vehicles": False, "shuffle_items": False, "speed_stages": 0}),
    ("max_goal", {"goal_cups": 24, "starting_cups": 4, "speed_stages": 5, "win_requires_speed": 5}),
    ("base_game_max_goal", {"course_selection": "base_game", "goal_cups": 24}),
    ("bcp_only_max_goal", {"course_selection": "booster_course_pass", "goal_cups": 24, "starting_cups": 4}),
]


@pytest.mark.parametrize("name,overrides", SCENARIOS)
def test_generation(_apworld_installed, name, overrides):
    with tempfile.TemporaryDirectory(prefix=f"mk8_gen_{name}_") as td:
        d = Path(td)
        (d / "Mario.yaml").write_text(_yaml(overrides), encoding="utf-8")
        r = _generate(d)
        if r.returncode != 0 or "Total Time:" not in r.stdout:
            tail = "\n".join(r.stdout.splitlines()[-40:])
            pytest.fail(f"generation failed for {name} (exit={r.returncode})\n{tail}\n{r.stderr.strip()}")
