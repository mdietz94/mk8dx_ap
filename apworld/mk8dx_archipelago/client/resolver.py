"""Turn a race event (PlayReport or hook) into AP location names.

Pure functions so the mapping is unit-testable without asyncio / AP.
"""

from __future__ import annotations

from dataclasses import dataclass

from . import tables
from .learned import LearnedIds, course_for_internal
from .play_report import RaceReport

# gear::ERaceRule
RULE_GRAND_PRIX = 0
RULE_VS = 1
RULE_TIME_ATTACK = 2
RULE_BATTLE = 3

_RULE_NAMES = {"GrandPrix": RULE_GRAND_PRIX, "GP": RULE_GRAND_PRIX, "VS": RULE_VS,
               "Versus": RULE_VS, "TimeAttack": RULE_TIME_ATTACK, "TimeTrial": RULE_TIME_ATTACK,
               "Battle": RULE_BATTLE}


@dataclass
class Resolved:
    course: tables.Course | None
    rank: int            # 1-based
    rule: int            # gear::ERaceRule or -1
    locations: list[str]
    note: str = ""


def rule_from_text(s: str) -> int:
    if not s:
        return -1
    for k, v in _RULE_NAMES.items():
        if k.lower() in s.lower():
            return v
    return -1


def locations_for(course: tables.Course, rank_1based: int, tiers: tuple[str, ...]) -> list[str]:
    out: list[str] = []
    if "Finish" in tiers:
        out.append(tables.course_location(course, "Finish"))
    if rank_1based <= 3 and "Top 3" in tiers:
        out.append(tables.course_location(course, "Top 3"))
    if rank_1based == 1 and "Win" in tiers:
        out.append(tables.course_location(course, "Win"))
    return out


def resolve_report(rep: RaceReport, tiers: tuple[str, ...] = tables.LOCATION_TIERS,
                   allowed_rules: tuple[int, ...] = (RULE_GRAND_PRIX, RULE_VS)) -> Resolved:
    rule = rule_from_text(rep.rule)
    course = course_for_internal(rep.course)
    if course is None:
        return Resolved(None, rep.rank, rule, [], f"unknown course {rep.course!r}")
    if not rep.finished:
        return Resolved(course, rep.rank, rule, [], f"finish reason {rep.finish_reason!r}")
    if rule != -1 and rule not in allowed_rules:
        return Resolved(course, rep.rank, rule, [], f"rule {rep.rule!r} not tracked")
    if rep.rank <= 0:
        return Resolved(course, rep.rank, rule, [], "no rank")
    return Resolved(course, rep.rank, rule, locations_for(course, rep.rank, tiers))


def resolve_hook(course_id: int, rank_0based: int, rule: int, learned: LearnedIds,
                 tiers: tuple[str, ...] = tables.LOCATION_TIERS,
                 allowed_rules: tuple[int, ...] = (RULE_GRAND_PRIX, RULE_VS)) -> Resolved:
    rank = rank_0based + 1
    course = learned.course_for_id(course_id)
    if course is None:
        return Resolved(None, rank, rule, [], f"course id {course_id} not learned yet")
    if rule not in allowed_rules:
        return Resolved(course, rank, rule, [], f"rule {rule} not tracked")
    if rank_0based < 0:
        return Resolved(course, rank, rule, [], "no rank")
    return Resolved(course, rank, rule, locations_for(course, rank, tiers))


class GrandPrixTracker:
    """Detects "all four courses of cup X finished in one Grand Prix".

    Feed every race in order; returns the cup when the fourth consecutive
    GP race completes the cup's course set.
    """

    def __init__(self) -> None:
        self._runs: dict[int, set[int]] = {}

    def observe(self, course: tables.Course, rule: int) -> tables.Cup | None:
        if rule != RULE_GRAND_PRIX:
            # A second report of a race already counted in the current run
            # (the hook and PlayReport channels both fire per race, and the
            # hook path may carry a stale rule) must not abandon the run.
            if course.slot in self._runs.get(course.cup, set()):
                return None
            self._runs.clear()
            return None
        slots = self._runs.setdefault(course.cup, set())
        if course.slot == 0 and slots != {0}:
            slots.clear()
        slots.add(course.slot)
        # A GP is sequential; any other cup in flight is abandoned.
        for k in list(self._runs):
            if k != course.cup:
                del self._runs[k]
        if slots == {0, 1, 2, 3}:
            self._runs.pop(course.cup, None)
            return tables.CUPS[course.cup]
        return None
