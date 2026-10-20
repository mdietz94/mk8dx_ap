"""The client sends CLIENT_GOAL once enough cups are completed, keeps it
across reconnects (CommonContext.finished_game) and never carries it over to
a different slot."""

import sys
from pathlib import Path

import pytest


def _find_archipelago() -> Path | None:
    for parent in Path(__file__).resolve().parents:
        cand = parent / "vendor" / "Archipelago"
        if (cand / "CommonClient.py").exists():
            return cand
    return None


_AP = _find_archipelago()
if _AP is not None and str(_AP) not in sys.path:
    sys.path.insert(0, str(_AP))
try:  # pragma: no cover
    import ModuleUpdate  # type: ignore[import-not-found]
    ModuleUpdate.update_ran = True
except ImportError:
    pass


def _ctx(goal_trophies: int):
    pytest.importorskip("CommonClient", reason="Archipelago checkout not present")
    from client.context import MK8DXContext
    from client.state import BridgeState

    ctx = MK8DXContext(None, None, BridgeState())
    ctx.slot_data = {"goal_trophies": goal_trophies}
    ctx.seed_name, ctx.auth = "seed1", "Mario"
    sent: list = []

    async def record(msgs):
        sent.extend(msgs)
    ctx.send_msgs = record  # type: ignore[assignment]
    return ctx, sent


def _goals(sent) -> int:
    from NetUtils import ClientStatus
    return sum(1 for m in sent if m.get("cmd") == "StatusUpdate" and m.get("status") == ClientStatus.CLIENT_GOAL)


@pytest.mark.asyncio
async def test_goal_sent_once_when_enough_cups():
    ctx, sent = _ctx(2)
    ctx.state.checked_locations |= {"Complete Mushroom Cup (Grand Prix)", "Mario Kart Stadium: Win"}
    await ctx._maybe_goal()
    assert _goals(sent) == 0 and not ctx.finished_game

    ctx.state.checked_locations.add("Complete Flower Cup (Grand Prix)")
    await ctx._maybe_goal()
    await ctx._maybe_goal()
    assert _goals(sent) == 1 and ctx.finished_game


@pytest.mark.asyncio
async def test_goal_survives_disconnect_but_not_slot_change():
    ctx, _ = _ctx(1)
    from CommonClient import CommonContext
    ctx.send_msgs = CommonContext.send_msgs.__get__(ctx)  # type: ignore[assignment]  # no server: dropped
    ctx.state.checked_locations.add("Complete Mushroom Cup (Grand Prix)")
    await ctx._maybe_goal()
    assert ctx.finished_game  # CommonClient re-sends CLIENT_GOAL on the next Connected

    async def noop(*_a, **_k):
        return None
    ctx.get_username = noop  # type: ignore[assignment]
    ctx.send_connect = noop  # type: ignore[assignment]

    await ctx.server_auth()  # same slot reconnecting
    assert ctx.finished_game
    ctx.auth = "Luigi"
    await ctx.server_auth()  # another slot from the same client
    assert not ctx.finished_game
