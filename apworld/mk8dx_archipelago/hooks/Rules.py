"""Rule functions referenced from data/locations.json `requires` strings as
`{FuncName()}`. Signature: (world, multiworld, state, player, *args)."""

from __future__ import annotations

from BaseClasses import CollectionState, MultiWorld
from worlds.AutoWorld import World

from ..Helpers import get_option_value

PROGRESSIVE_SPEED = "Progressive Speed"
TROPHY = "Trophy"


def _win_speed_needed(multiworld: MultiWorld, player: int) -> int:
    stages = int(get_option_value(multiworld, player, "speed_stages"))
    need = int(get_option_value(multiworld, player, "win_requires_speed"))
    return max(0, min(need, stages))


def HasWinSpeed(world: World, multiworld: MultiWorld, state: CollectionState, player: int) -> bool:
    """Enough Progressive Speed to expect a 1st-place finish."""
    return state.count(PROGRESSIVE_SPEED, player) >= _win_speed_needed(multiworld, player)


def _goal_trophies(world: World, multiworld: MultiWorld, player: int) -> int:
    want = int(get_option_value(multiworld, player, "goal_cups"))
    available = getattr(world, "cup_completion_count", None)
    if available is None:
        available = 24 if get_option_value(multiworld, player, "include_booster_course_pass") else 12
    return max(1, min(want, available))


def GoalTrophies(world: World, multiworld: MultiWorld, state: CollectionState, player: int) -> bool:
    return state.count(TROPHY, player) >= _goal_trophies(world, multiworld, player)
