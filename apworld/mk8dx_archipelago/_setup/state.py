"""`setup_state.json`: what the last setup run did, so a re-run starts on the
same target and the client can tell whether the deployed mod is current."""

from __future__ import annotations

import json
import time
from pathlib import Path
from typing import Any

from . import setup_state_path


def load() -> dict[str, Any]:
    try:
        data = json.loads(setup_state_path().read_text(encoding="utf-8-sig"))
        return data if isinstance(data, dict) else {}
    except (OSError, ValueError):
        return {}


def save(state: dict[str, Any]) -> None:
    path = setup_state_path()
    tmp = path.with_suffix(".tmp")
    tmp.write_text(json.dumps(state, indent=2, sort_keys=True), encoding="utf-8")
    tmp.replace(path)


def record_deploy(target: str, root: Path, mod_version: str) -> dict[str, Any]:
    state = load()
    state.update({
        "target": target,
        f"{target}_root": str(root),
        "deployed_version": mod_version,
        "deployed_at": time.strftime("%Y-%m-%d %H:%M:%S"),
    })
    save(state)
    return state


def is_first_run() -> bool:
    """No deploy has ever been recorded on this machine."""
    return not load().get("deployed_version")
