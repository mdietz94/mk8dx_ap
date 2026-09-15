"""Consistency between client/tables.py, the generated data/*.json, and the
Switch mod's symbol table."""

import json
import subprocess
import sys
from pathlib import Path

from client import tables

APWORLD = Path(__file__).resolve().parent.parent
REPO = APWORLD.parent.parent


def test_tables_shape():
    assert len(tables.CUPS) == 24 and [c.index for c in tables.CUPS] == list(range(24))
    assert len(tables.COURSES) == 96
    for cup in tables.CUPS:
        assert sorted(c.slot for c in tables.COURSES if c.cup == cup.index) == [0, 1, 2, 3]
    assert len(tables.ITEMS) == 23 and [e.default_id for e in tables.ITEMS] == list(range(23))
    names = [c.name for c in tables.COURSES]
    assert len(names) == len(set(names))
    internals = [c.internal for c in tables.COURSES if c.internal]
    assert len(internals) == len(set(internals))
    for lst in (tables.DRIVERS, tables.BODIES, tables.TIRES, tables.GLIDERS):
        ids = [e.default_id for e in lst]
        assert len(ids) == len(set(ids))


def test_generated_data_matches_tables():
    r = subprocess.run([sys.executable, str(REPO / "scripts" / "gen_apworld_data.py"), "--check"],
                       capture_output=True, text=True)
    assert r.returncode == 0, r.stdout + r.stderr


def test_symbol_table_fresh():
    r = subprocess.run([sys.executable, str(REPO / "scripts" / "gen_symbol_table.py"), "--check"],
                       capture_output=True, text=True)
    assert r.returncode == 0, r.stdout + r.stderr


def test_locations_cover_every_course_and_cup():
    locs = {l["name"] for l in json.loads((APWORLD / "data" / "locations.json").read_text(encoding="utf-8"))}
    for c in tables.COURSES:
        for tier in tables.LOCATION_TIERS:
            assert tables.course_location(c, tier) in locs
    for cup in tables.CUPS:
        assert tables.cup_complete_location(cup) in locs
    items = {i["name"] for i in json.loads((APWORLD / "data" / "items.json").read_text(encoding="utf-8"))}
    for cup in tables.CUPS:
        assert tables.cup_item(cup) in items
    for name in (tables.STARTING_DRIVER,):
        assert f"Character: {name}" in items
    assert tables.PROGRESSIVE_SPEED in items and tables.TROPHY in items
