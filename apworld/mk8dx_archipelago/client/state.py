"""Client-side state: received AP items -> restriction snapshot, plus a
small log/telemetry mirror for the GUI. Thread-affine to the asyncio loop.
"""

from __future__ import annotations

import logging
import time
from dataclasses import dataclass, field

from . import tables
from .learned import LearnedIds
from .protocol import StateMsg

log = logging.getLogger("MK8DX")


@dataclass
class RaceEvent:
    ts: float
    source: str        # "prepo" | "hook"
    course: str        # AP course name or "?"
    rank: int          # 1-based
    rule: str
    engine: str
    checked: list[str] = field(default_factory=list)


class BridgeState:
    """Pure bookkeeping; no I/O."""

    def __init__(self, learned: LearnedIds | None = None):
        self.learned = learned or LearnedIds()
        self.slot: str = ""
        self.seed: str = ""
        self.ap_conn: str = "disconnected"
        self.switch_conn: str = "disconnected"
        self.switch_info: dict = {}
        self.received: dict[str, int] = {}   # AP item name -> count
        self.checked_locations: set[str] = set()
        self.recent_races: list[RaceEvent] = []
        self.logs: list[str] = []
        self.state_seq = 0
        # Options from slot_data
        self.enabled = True
        self.shuffle_characters = True
        self.shuffle_vehicles = True
        self.shuffle_items = True
        self.speed_stages = 3
        self.min_speed_pct = 60
        self.cpu_items = False
        self.starting_cup = tables.STARTING_CUP

    # ---- items ----
    def add_item(self, name: str, count: int = 1) -> None:
        self.received[name] = self.received.get(name, 0) + count

    def clear_items(self) -> None:
        self.received.clear()

    def has(self, name: str) -> bool:
        return self.received.get(name, 0) > 0

    def count(self, name: str) -> int:
        return self.received.get(name, 0)

    def apply_slot_data(self, slot_data: dict) -> None:
        self.shuffle_characters = bool(slot_data.get("shuffle_characters", True))
        self.shuffle_vehicles = bool(slot_data.get("shuffle_vehicles", True))
        self.shuffle_items = bool(slot_data.get("shuffle_items", True))
        self.speed_stages = int(slot_data.get("speed_stages", 3))
        self.min_speed_pct = int(slot_data.get("min_speed_percent", 60))
        self.cpu_items = bool(slot_data.get("restrict_cpu_items", False))
        self.starting_cup = str(slot_data.get("starting_cup", tables.STARTING_CUP))

    # ---- restriction snapshot ----
    def speed_pct(self) -> int:
        stages = max(0, self.speed_stages)
        if stages == 0:
            return 100
        have = min(self.count(tables.PROGRESSIVE_SPEED), stages)
        lo = max(1, min(100, self.min_speed_pct))
        return int(round(lo + (100 - lo) * have / stages))

    def build_state(self) -> StateMsg:
        L = self.learned

        def mask_for(kind: str, entries: list[tables.Entry], item_fn, words: int,
                     always: str | None = None, shuffled: bool = True) -> list[int]:
            mask = [0] * words
            for e in entries:
                allowed = (not shuffled) or self.has(item_fn(e)) or e.name == always
                if not allowed:
                    continue
                id_ = L.id_for_entry(kind, e)
                if 0 <= id_ < 32 * words:
                    mask[id_ // 32] |= 1 << (id_ % 32)
            return mask

        cups = 0
        for cup in tables.CUPS:
            if self.has(tables.cup_item(cup)) or cup.name == self.starting_cup:
                cups |= 1 << cup.index
        items = 0
        for e in tables.ITEMS:
            if (not self.shuffle_items) or self.has(tables.item_item(e)) or e.name in tables.STARTING_ITEMS:
                items |= 1 << e.default_id
        drivers = mask_for("driver", tables.DRIVERS, tables.driver_item, 4,
                           tables.STARTING_DRIVER, self.shuffle_characters)
        bodies = mask_for("body", tables.BODIES, tables.body_item, 2,
                          tables.STARTING_BODY, self.shuffle_vehicles)
        tires = mask_for("tire", tables.TIRES, tables.tire_item, 1,
                         tables.STARTING_TIRE, self.shuffle_vehicles)[0]
        wings = mask_for("wing", tables.GLIDERS, tables.glider_item, 1,
                         tables.STARTING_GLIDER, self.shuffle_vehicles)[0]

        def default_id(kind: str, entries: list[tables.Entry], name: str) -> int:
            for e in entries:
                if e.name == name:
                    return L.id_for_entry(kind, e)
            return 0

        # Restrictions apply only while an AP slot is connected. Before that
        # (or after a disconnect) the Switch must run vanilla: with no items
        # received every mask would otherwise collapse to the starting
        # unlocks (one cup, 60% speed) -- observed on the first 4.0.0 boot
        # with the client up but no server.
        enabled = self.enabled and self.ap_conn == "ready"
        self.state_seq += 1
        return StateMsg(
            seq=self.state_seq,
            enabled=enabled,
            cups=cups & 0xFFFFFFFF,
            drivers=[m & 0xFFFFFFFF for m in drivers],
            bodies=[m & 0xFFFFFFFF for m in bodies],
            tires=tires & 0xFFFFFFFF,
            wings=wings & 0xFFFFFFFF,
            items=items & 0xFFFFFFFF,
            speed_pct=self.speed_pct(),
            engine_max=3,
            def_driver=default_id("driver", tables.DRIVERS, tables.STARTING_DRIVER),
            def_body=default_id("body", tables.BODIES, tables.STARTING_BODY),
            def_tire=default_id("tire", tables.TIRES, tables.STARTING_TIRE),
            def_wing=default_id("wing", tables.GLIDERS, tables.STARTING_GLIDER),
            cpu_items=self.cpu_items,
        )

    # ---- telemetry ----
    def add_race(self, ev: RaceEvent) -> None:
        self.recent_races.append(ev)
        del self.recent_races[:-50]

    def add_log(self, text: str) -> None:
        self.logs.append(f"{time.strftime('%H:%M:%S')} {text}")
        del self.logs[:-500]

    def summary(self) -> dict:
        return {
            "slot": self.slot,
            "seed": self.seed,
            "ap": self.ap_conn,
            "switch": self.switch_conn,
            "switch_info": dict(self.switch_info),
            "items": dict(self.received),
            "checked": len(self.checked_locations),
            "speed_pct": self.speed_pct(),
            "learned": {k: len(m.learned) for k, m in self.learned.maps.items()},
        }
