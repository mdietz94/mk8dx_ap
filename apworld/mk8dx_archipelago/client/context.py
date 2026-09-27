"""MK8DXContext: the Archipelago CommonContext subclass.

Owns the AP websocket side, translates received items into the restriction
snapshot pushed to the Switch, and turns Switch race events into
LocationChecks.
"""

from __future__ import annotations

import asyncio
import logging
import time
from typing import Any

from CommonClient import ClientCommandProcessor, CommonContext
from NetUtils import ClientStatus

from . import tables
from .learned import course_for_internal
from .play_report import parse_race_report
from .protocol import PlayReportMsg, RaceResultMsg
from .resolver import GrandPrixTracker, resolve_hook, resolve_report
from .state import BridgeState, RaceEvent

log = logging.getLogger("MK8DX")

PAIR_WINDOW_S = 30.0


class MK8DXCommandProcessor(ClientCommandProcessor):
    ctx: "MK8DXContext"

    def _cmd_mk8_status(self) -> bool:
        """Show MK8DX client state (items, Switch link, learned ids)."""
        s = self.ctx.state.summary()
        self.output(f"AP: {s['ap']}  Switch: {s['switch']} {s['switch_info']}")
        self.output(f"speed: {s['speed_pct']}%  items received: {sum(s['items'].values())}  "
                    f"checked: {s['checked']}  learned ids: {s['learned']}")
        return True

    def _cmd_mk8_push(self) -> bool:
        """Re-send the restriction state to the Switch."""
        asyncio.create_task(self.ctx.push_state())
        return True

    def _cmd_mk8_races(self) -> bool:
        """List the last few race events."""
        for ev in self.ctx.state.recent_races[-10:]:
            self.output(f"{time.strftime('%H:%M:%S', time.localtime(ev.ts))} [{ev.source}] "
                        f"{ev.course} rank={ev.rank} rule={ev.rule} -> {', '.join(ev.checked) or '(no check)'}")
        return True


class MK8DXContext(CommonContext):
    game = tables.GAME_NAME
    items_handling = 0b111
    command_processor = MK8DXCommandProcessor
    want_slot_data = True

    def __init__(self, server_address: str | None, password: str | None, state: BridgeState):
        super().__init__(server_address, password)
        self.state = state
        self.switch = None  # SwitchServer, set by main
        self.slot_data: dict = {}
        self._gp = GrandPrixTracker()
        self._seen_reports: set[str] = set()
        self._seen_hook: dict[str, float] = {}
        # Pairing window for learning course ids: the hook reports a numeric
        # course id, the PlayReport reports the course string, both for the
        # same race within a few seconds of each other.
        self._last_hook_course: tuple[int, float] | None = None
        self._last_report_course: tuple[str, float] | None = None
        self._goal_sent = False
        self._tiers: tuple[str, ...] = tables.LOCATION_TIERS

    # ---- AP plumbing ----
    async def server_auth(self, password_requested: bool = False):
        if password_requested and not self.password:
            await super().server_auth(password_requested)
        await self.get_username()
        await self.send_connect()

    def on_package(self, cmd: str, args: dict) -> None:
        if cmd == "Connected":
            self.slot_data = args.get("slot_data", {}) or {}
            self.state.slot = self.player_names.get(self.slot, self.auth or "")
            self.state.seed = str(getattr(self, "seed_name", "") or args.get("seed_name", "") or "")
            self.state.ap_conn = "ready"
            self.state.apply_slot_data(self.slot_data)
            self._tiers = tuple(
                t for t in tables.LOCATION_TIERS
                if not (t == "Top 3" and not self.slot_data.get("include_top3_locations", True))
                and not (t == "Win" and not self.slot_data.get("include_win_locations", True)))
            self.state.clear_items()
            for loc_id in args.get("checked_locations", []):
                name = self.location_names.lookup_in_game(loc_id) if hasattr(self.location_names, "lookup_in_game") else self.location_names.get(loc_id)
                if name:
                    self.state.checked_locations.add(name)
            log.info("connected as %s (seed %s); tiers=%s", self.state.slot, self.state.seed, self._tiers)
            asyncio.create_task(self.push_state())
        elif cmd == "ReceivedItems":
            if args.get("index", 0) == 0:
                self.state.clear_items()
            names = []
            for ni in args.get("items", []):
                name = self._item_name(ni.item)
                if name:
                    self.state.add_item(name)
                    names.append(name)
            log.info("received %d item(s) (index %s): %s", len(names), args.get("index", 0),
                     ", ".join(names[:12]) + (" ..." if len(names) > 12 else ""))
            asyncio.create_task(self.push_state())
        elif cmd == "RoomUpdate":
            for loc_id in args.get("checked_locations", []):
                name = self._location_name(loc_id)
                if name:
                    self.state.checked_locations.add(name)

    async def disconnect(self, allow_autoreconnect: bool = False):
        self.state.ap_conn = "disconnected"
        await super().disconnect(allow_autoreconnect)

    def _item_name(self, item_id: int) -> str | None:
        try:
            return self.item_names.lookup_in_game(item_id, self.game)
        except Exception:
            try:
                return self.item_names[item_id]
            except Exception:
                return None

    def _location_name(self, loc_id: int) -> str | None:
        try:
            return self.location_names.lookup_in_game(loc_id, self.game)
        except Exception:
            try:
                return self.location_names[loc_id]
            except Exception:
                return None

    def _location_id(self, name: str) -> int | None:
        """AP location id for one of our location names.

        The apworld itself is importable in the client process (the client
        lives inside it), so its static name -> id table is the source of
        truth; the AutoWorldRegister lookup covers the zip-installed case."""
        table = getattr(self, "_loc_name_to_id", None)
        if table is None:
            table = {}
            try:
                from worlds.AutoWorld import AutoWorldRegister
                table = dict(AutoWorldRegister.world_types[self.game].location_name_to_id)
            except Exception:
                try:
                    from ..Locations import location_name_to_id as _t
                    table = dict(_t)
                except Exception:
                    log.exception("could not load the location id table")
            self._loc_name_to_id = table
        return table.get(name)

    # ---- Switch side ----
    async def push_state(self) -> None:
        if self.switch is not None:
            await self.switch.push_state()

    def _pair_course_id(self) -> None:
        h, p = self._last_hook_course, self._last_report_course
        if h is None or p is None or abs(h[1] - p[1]) > PAIR_WINDOW_S:
            return
        if course_for_internal(p[0]) is None:
            return
        if self.state.learned.learn_course(h[0], p[0]):
            log.info("learned course id %d = %s (paired hook + PlayReport)", h[0], p[0])
        self._last_hook_course = self._last_report_course = None

    async def on_race_result(self, msg: RaceResultMsg) -> None:
        self._last_hook_course = (msg.course_id, time.time())
        self._pair_course_id()
        r = resolve_hook(msg.course_id, msg.rank, msg.rule, self.state.learned, self._tiers)
        key = f"hook|{msg.course_id}|{msg.kart_index}"
        now = time.time()
        if now - self._seen_hook.get(key, 0) < 30:
            return  # same race reported twice
        self._seen_hook[key] = now
        course_name = r.course.name if r.course else f"course id {msg.course_id}"
        log.info("race_result[hook] %s rank=%d rule=%d -> %s %s", course_name, r.rank, msg.rule,
                 r.locations or "(none)", r.note)
        await self._credit(r.course, r.rank, str(msg.rule), r.locations, "hook", msg.rule)

    async def on_play_report(self, msg: PlayReportMsg) -> None:
        rep = parse_race_report(msg.payload())
        if rep is None:
            log.info("play_report room=%s (%d bytes): not a race record", msg.room, msg.size)
            return
        log.info("play_report race: course=%s rank=%d rule=%s engine=%s finish=%s mode=%s driver=%s body=%s",
                 rep.course, rep.rank, rep.rule, rep.engine, rep.finish_reason, rep.mode, rep.driver, rep.body)
        if rep.key in self._seen_reports:
            return
        self._seen_reports.add(rep.key)
        self._last_report_course = (rep.course, time.time())
        self._pair_course_id()
        r = resolve_report(rep, self._tiers)
        if r.course is None:
            log.warning("play_report course %r is not in the course table; "
                        "add its internal name to client/tables.py", rep.course)
        await self._credit(r.course, r.rank, rep.rule, r.locations, "prepo", r.rule)

    async def _credit(self, course, rank: int, rule_text: str, locations: list[str],
                      source: str, rule: int) -> None:
        checked: list[str] = []
        if course is not None:
            cup = self._gp.observe(course, rule)
            if cup is not None:
                locations = list(locations) + [tables.cup_complete_location(cup)]
        for name in locations:
            if name in self.state.checked_locations:
                continue
            loc_id = self._location_id(name)
            if loc_id is None:
                log.warning("no AP id for location %r (not in this seed?)", name)
                continue
            self.state.checked_locations.add(name)
            checked.append(name)
            await self.check_locations([loc_id])
        self.state.add_race(RaceEvent(time.time(), source, course.name if course else "?", rank,
                                      rule_text, "", checked))
        if checked:
            log.info("checked: %s", ", ".join(checked))
        await self._maybe_goal()

    async def _maybe_goal(self) -> None:
        if self._goal_sent:
            return
        need = int(self.slot_data.get("goal_trophies", 0) or 0)
        if need <= 0:
            return
        have = sum(1 for n in self.state.checked_locations
                   if n.startswith("Complete ") and n.endswith("(Grand Prix)"))
        if have >= need:
            self._goal_sent = True
            log.info("goal reached: %d/%d cups completed", have, need)
            await self.send_msgs([{"cmd": "StatusUpdate", "status": ClientStatus.CLIENT_GOAL}])

    # ---- GUI ----
    def run_gui(self) -> None:
        from .gui import MK8DXManager
        self.ui = MK8DXManager(self)
        self.ui_task = asyncio.create_task(self.ui.async_run(), name="UI")
