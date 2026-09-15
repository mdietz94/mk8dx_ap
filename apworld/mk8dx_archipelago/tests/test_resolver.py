from client import tables
from client.learned import LearnedIds, course_for_internal
from client.play_report import RaceReport
from client.resolver import (RULE_GRAND_PRIX, RULE_TIME_ATTACK, RULE_VS, GrandPrixTracker,
                             resolve_hook, resolve_report, rule_from_text)


def test_course_for_internal_variants():
    c = course_for_internal("tc_Course_Gu_FirstCircuit")
    assert c is not None and c.name == "Mario Kart Stadium"
    assert course_for_internal("Gu_FirstCircuit").name == "Mario Kart Stadium"
    assert course_for_internal("gu_firstcircuit_x").name == "Mario Kart Stadium"
    assert course_for_internal("Cnsw_68").name == "Wii Rainbow Road"
    assert course_for_internal("Nope_Zzz") is None


def test_rule_from_text():
    assert rule_from_text("VS") == RULE_VS
    assert rule_from_text("GrandPrix") == RULE_GRAND_PRIX
    assert rule_from_text("TimeAttack") == RULE_TIME_ATTACK
    assert rule_from_text("") == -1


def test_resolve_report_win_top3_finish():
    rep = RaceReport(course="Gu_FirstCircuit", rank=1, finish_reason="Finish", rule="VS", end_time="1")
    r = resolve_report(rep)
    assert r.course.name == "Mario Kart Stadium"
    assert r.locations == ["Mario Kart Stadium - Finish", "Mario Kart Stadium - Top 3", "Mario Kart Stadium - Win"]
    rep.rank = 3
    assert resolve_report(rep).locations == ["Mario Kart Stadium - Finish", "Mario Kart Stadium - Top 3"]
    rep.rank = 9
    assert resolve_report(rep).locations == ["Mario Kart Stadium - Finish"]
    assert resolve_report(rep, tiers=("Finish",)).locations == ["Mario Kart Stadium - Finish"]


def test_resolve_report_filters():
    rep = RaceReport(course="Gu_FirstCircuit", rank=1, finish_reason="Retire", rule="VS")
    assert resolve_report(rep).locations == []
    rep = RaceReport(course="Gu_FirstCircuit", rank=1, finish_reason="Finish", rule="TimeAttack")
    assert resolve_report(rep).locations == []
    rep = RaceReport(course="Unknown_X", rank=1, rule="VS")
    r = resolve_report(rep)
    assert r.course is None and r.locations == [] and "unknown course" in r.note


def test_resolve_hook_uses_learned_ids():
    L = LearnedIds()
    r = resolve_hook(12, 0, RULE_VS, L)
    assert r.course is None and "not learned" in r.note
    L.learn_course(12, "tc_Course_Gu_Cake")
    r = resolve_hook(12, 0, RULE_VS, L)
    assert r.course.name == "Sweet Sweet Canyon" and r.rank == 1
    assert "Sweet Sweet Canyon - Win" in r.locations
    assert resolve_hook(12, 4, RULE_VS, L).locations == ["Sweet Sweet Canyon - Finish"]
    assert resolve_hook(12, 0, RULE_TIME_ATTACK, L).locations == []


def test_grand_prix_tracker():
    t = GrandPrixTracker()
    mushroom = [c for c in tables.COURSES if c.cup == 0]
    assert t.observe(mushroom[0], RULE_GRAND_PRIX) is None
    assert t.observe(mushroom[1], RULE_GRAND_PRIX) is None
    assert t.observe(mushroom[2], RULE_GRAND_PRIX) is None
    assert t.observe(mushroom[3], RULE_GRAND_PRIX).name == "Mushroom Cup"
    # VS races never complete a cup and reset progress
    assert t.observe(mushroom[0], RULE_GRAND_PRIX) is None
    assert t.observe(mushroom[1], RULE_VS) is None
    assert t.observe(mushroom[2], RULE_GRAND_PRIX) is None
    assert t.observe(mushroom[3], RULE_GRAND_PRIX) is None
    # ...but a duplicate report of an already-counted race (second channel,
    # possibly with a stale rule) does not abandon the run
    t = GrandPrixTracker()
    for c in mushroom[:3]:
        assert t.observe(c, RULE_GRAND_PRIX) is None
        assert t.observe(c, RULE_VS) is None
        assert t.observe(c, RULE_GRAND_PRIX) is None
    assert t.observe(mushroom[3], RULE_GRAND_PRIX).name == "Mushroom Cup"
