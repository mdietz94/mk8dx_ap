"""Course ids are learned by pairing the two race channels, and the client
explains which features an unresolved game function turns off."""

import sys
from pathlib import Path

import pytest


def _find_archipelago() -> Path | None:
    for parent in Path(__file__).resolve().parents:
        cand = parent / "vendor" / "Archipelago"
        if (cand / "CommonClient.py").exists():
            return cand
    return None


def test_describe_unresolved():
    from client.switch_server import describe_unresolved
    assert describe_unresolved("") == []
    assert describe_unresolved("gear::ItemOwner::startSlot") == ["item-box restriction"]
    got = describe_unresolved("gear::FlagAccessor::isOpen,gear::DLCManager::getCupStatus,nope::x")
    assert got == ["cup locking"]
    assert describe_unresolved("gear::FlagAccessor::isOpen(EBodyID)") == ["received karts selectable in menus"]
    assert describe_unresolved("gear::GetCurrentUserSaveData") == []


_AP = _find_archipelago()
if _AP is not None and str(_AP) not in sys.path:
    sys.path.insert(0, str(_AP))
try:  # pragma: no cover
    import ModuleUpdate  # type: ignore[import-not-found]
    ModuleUpdate.update_ran = True
except ImportError:
    pass


@pytest.mark.asyncio
async def test_hook_and_report_pair_teaches_course_id():
    pytest.importorskip("CommonClient", reason="Archipelago checkout not present")
    from client.context import MK8DXContext
    from client.play_report import RaceReport
    from client.protocol import RaceResultMsg
    from client.state import BridgeState

    state = BridgeState()
    ctx = MK8DXContext(None, None, state)

    async def no_check(_locs):
        return None
    ctx.check_locations = no_check  # type: ignore[assignment]
    ctx._location_id = lambda name: None  # type: ignore[assignment]

    assert state.learned.course_for_id(37) is None
    # Hook fires first (finish line), PlayReport a moment later (results).
    await ctx.on_race_result(RaceResultMsg(seq=1, course_id=37, rule=1, engine=2, rank=0, kart_index=0))
    assert state.learned.course_for_id(37) is None
    ctx._last_report_course = ("Gu_Cake", __import__("time").time())
    ctx._pair_course_id()
    assert state.learned.course_for_id(37).name == "Sweet Sweet Canyon"

    # Unknown course strings are not learned; stale pairs are ignored.
    ctx._last_hook_course = (38, 0.0)
    ctx._last_report_course = ("Gu_WaterPark", 1000.0)
    ctx._pair_course_id()
    assert state.learned.course_for_id(38) is None
    ctx._last_hook_course = (39, 5.0)
    ctx._last_report_course = ("Not_A_Course", 6.0)
    ctx._pair_course_id()
    assert state.learned.course_for_id(39) is None
    assert RaceReport(course="x").course == "x"


@pytest.mark.asyncio
async def test_cup_check_needs_first_place_overall():
    pytest.importorskip("CommonClient", reason="Archipelago checkout not present")
    from client import tables
    from client.context import MK8DXContext
    from client.play_report import RaceReport
    from client.protocol import GpResultMsg
    from client.state import BridgeState

    state = BridgeState()
    ctx = MK8DXContext(None, None, state)
    sent: list[int] = []

    async def record(ids):
        sent.extend(ids)
    ctx.check_locations = record  # type: ignore[assignment]
    names: dict[str, int] = {}
    ctx._location_id = lambda name: names.setdefault(name, len(names) + 1)  # type: ignore[assignment]

    # Finishing all four Mushroom Cup races no longer completes the cup by itself.
    for c in [c for c in tables.COURSES if c.cup == 0]:
        ctx._seen_reports.clear()
        await ctx._credit(c, 5, "GP", [], "prepo", 0)
    assert "Complete Mushroom Cup (Grand Prix)" not in state.checked_locations

    await ctx.on_gp_result(GpResultMsg(seq=1, cup=0, engine=1, placing=2, points=50))
    assert "Complete Mushroom Cup (Grand Prix)" not in state.checked_locations
    assert sent == []
    await ctx.on_gp_result(GpResultMsg(seq=2, cup=0, engine=1, placing=1, points=54))
    assert "Complete Mushroom Cup (Grand Prix)" in state.checked_locations
    assert len(sent) == 1
    await ctx.on_gp_result(GpResultMsg(seq=3, cup=0, engine=1, placing=1, points=60))
    assert len(sent) == 1   # already checked
    await ctx.on_gp_result(GpResultMsg(seq=4, cup=99, engine=1, placing=1, points=60))
    assert len(sent) == 1
    assert RaceReport(course="x").course == "x"
