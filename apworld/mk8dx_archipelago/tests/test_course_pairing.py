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
