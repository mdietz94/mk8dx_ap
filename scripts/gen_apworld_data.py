#!/usr/bin/env python3
"""Render apworld/mk8dx_archipelago/data/{items,locations,regions,categories}.json
from the tables in apworld/mk8dx_archipelago/client/tables.py.

The JSON files are committed (the apworld must load without running this),
but tables.py is the single source of truth. `--check` fails when the
committed JSON is stale.
"""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
APWORLD = REPO / "apworld" / "mk8dx_archipelago"
sys.path.insert(0, str(APWORLD / "client"))

import tables  # noqa: E402  (imported from client/ as a top-level module)

DATA = APWORLD / "data"
BCP = "Booster Course Pass"


def build() -> dict[str, object]:
    items: list[dict] = []
    locations: list[dict] = []
    regions: dict[str, dict] = {}
    categories: dict[str, dict] = {
        BCP: {"yaml_option": ["include_booster_course_pass"]},
        "Top 3": {"yaml_option": ["include_top3_locations"]},
        "Win": {"yaml_option": ["include_win_locations"]},
        "Characters": {"yaml_option": ["shuffle_characters"]},
        "Vehicles": {"yaml_option": ["shuffle_vehicles"]},
        "Item Box": {"yaml_option": ["shuffle_items"]},
    }

    # ---- cups: one region + one unlock item + one completion location each
    for cup in tables.CUPS:
        cats = ["Cups"] + ([BCP] if cup.bcp else [])
        items.append({"name": tables.cup_item(cup), "progression": True, "category": cats, "count": 1})
        regions[cup.name] = {
            "requires": f"|{tables.cup_item(cup)}|",
            "connects_to": [],
            "starting": True,
        }
        locations.append({
            "name": tables.cup_complete_location(cup),
            "region": cup.name,
            "category": ["Cup Completion"] + ([BCP] if cup.bcp else []),
            "place_item": [tables.TROPHY],
        })

    # ---- courses: Finish / Top 3 / Win
    for course in tables.COURSES:
        cup = tables.CUPS[course.cup]
        base_cats = ["Courses", cup.name] + ([BCP] if cup.bcp else [])
        locations.append({
            "name": tables.course_location(course, "Finish"),
            "region": cup.name,
            "category": base_cats,
        })
        locations.append({
            "name": tables.course_location(course, "Top 3"),
            "region": cup.name,
            "category": base_cats + ["Top 3"],
        })
        locations.append({
            "name": tables.course_location(course, "Win"),
            "region": cup.name,
            "category": base_cats + ["Win"],
            "requires": "{HasWinSpeed()}",
        })

    # ---- trophies (placed at cup completions; count trimmed in hooks/World.py)
    items.append({"name": tables.TROPHY, "progression": True, "category": ["Trophies"],
                  "count": len(tables.CUPS)})

    # ---- unlock items
    for e in tables.DRIVERS:
        items.append({"name": tables.driver_item(e), "useful": True,
                      "category": ["Characters"] + ([BCP] if e.bcp else []), "count": 1})
    for e in tables.BODIES:
        items.append({"name": tables.body_item(e), "useful": True, "category": ["Vehicles", "Karts"], "count": 1})
    for e in tables.TIRES:
        items.append({"name": tables.tire_item(e), "useful": True, "category": ["Vehicles", "Tires"], "count": 1})
    for e in tables.GLIDERS:
        items.append({"name": tables.glider_item(e), "useful": True, "category": ["Vehicles", "Gliders"], "count": 1})
    for e in tables.ITEMS:
        items.append({"name": tables.item_item(e), "useful": True, "category": ["Item Box"], "count": 1})

    # ---- progressive speed (count trimmed to the speed_stages option)
    items.append({"name": tables.PROGRESSIVE_SPEED, "progression": True, "category": ["Speed"], "count": 5})

    # ---- victory
    locations.append({
        "name": "Trophy Collection Complete",
        "region": "MK8DX",
        "category": ["Goal"],
        "requires": "{GoalTrophies()}",
        "victory": True,
    })

    return {"items": items, "locations": locations, "regions": regions, "categories": categories}


def render(obj: object) -> str:
    return json.dumps(obj, indent=2, ensure_ascii=False) + "\n"


def main(argv: list[str] | None = None) -> int:
    p = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    p.add_argument("--check", action="store_true")
    args = p.parse_args(argv)
    out = build()
    stale = []
    for name, obj in out.items():
        path = DATA / f"{name}.json"
        text = render(obj)
        if args.check:
            if not path.exists() or path.read_text(encoding="utf-8") != text:
                stale.append(path.name)
        else:
            DATA.mkdir(parents=True, exist_ok=True)
            path.write_text(text, encoding="utf-8")
    if args.check:
        if stale:
            print(f"STALE: {', '.join(stale)}; run scripts/gen_apworld_data.py", file=sys.stderr)
            return 1
        print("OK: apworld data is up to date")
        return 0
    print(f"OK: {len(out['items'])} items, {len(out['locations'])} locations, "
          f"{len(out['regions'])} regions -> {DATA}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
