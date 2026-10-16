"""asyncio TCP server the Switch mod connects to.

One line-delimited JSON message per line (client/protocol.py). The server
answers HELLO with hello_ack + the current restriction state, forwards
race events to the context, learns id tables, and mirrors Switch log lines
into the "MK8DX" logger. Only one Switch is active at a time; a second
connection replaces the first (the mod reconnects on its own).
"""

from __future__ import annotations

import asyncio
import logging
import time
from typing import Any, Awaitable, Callable

from . import protocol
from .protocol import (CourseNameMsg, ErrMsg, HelloAckMsg, HelloMsg, IdTableMsg, LogMsg,
                       PingMsg, PlayReportMsg, PongMsg, PrintMsg, RaceResultMsg, StateMsg)
from .state import BridgeState

log = logging.getLogger("MK8DX")

SUPPORTED_GAME_VERSION = "4.0.0"

# Which feature each game function backs (mirrors switch-mod/src/hooks/).
_SYMBOL_FEATURES = {
    "gear::SaveDataFile::load": "early networking start (falls back to the first menu/race)",
    "ui::RaceWindow::onFakeGoal": "fallback race detection (PlayReport still works)",
    "gear::GetRaceInfo": "fallback race detection",
    "gear::FrameworkUtil::getCurrentGameScene": "fallback race detection, speed cap",
    "gear::NetworkUtil::getMyKartIndex": "fallback race detection, speed cap",
    "gear::FlagAccessor::isOpen": "cup locking",
    "gear::DLCManager::getCupStatus": "cup locking",
    "ui::Control_CupButton::setCup": "cup locking (menu display)",
    "gear::ItemOwner::startSlot": "item-box restriction",
    "ui::SetNextCourse": "character / vehicle enforcement",
    "gear::GetNextRaceInfo": "character / vehicle enforcement",
    "ui::Control_RaceLapCoin::onCalc_": "speed cap",
    "gear::GetCurrentUserSaveData": "(unused)",
    "mush::EDriverID::text_": "character id calibration",
    "mush::EBodyID::text_": "kart id calibration",
    "mush::ETireID::text_": "tire id calibration",
    "mush::EWingID::text_": "glider id calibration",
}


def describe_unresolved(names: str) -> list[str]:
    """Feature descriptions for a comma-separated unresolved-symbol list."""
    out: list[str] = []
    for n in (names or "").split(","):
        f = _SYMBOL_FEATURES.get(n.strip())
        if f and f not in out and f != "(unused)":
            out.append(f)
    return out


RaceResultHandler = Callable[[RaceResultMsg], Awaitable[None]]
PlayReportHandler = Callable[[PlayReportMsg], Awaitable[None]]
StateProvider = Callable[[], StateMsg]


class SwitchServer:
    def __init__(self, host: str, port: int, state: BridgeState, *,
                 client_ver: str,
                 on_race_result: RaceResultHandler,
                 on_play_report: PlayReportHandler,
                 build_state: StateProvider,
                 on_switch_changed: Callable[[], None] | None = None) -> None:
        self.host = host
        self.port = port
        self.state = state
        self.client_ver = client_ver
        self._on_race_result = on_race_result
        self._on_play_report = on_play_report
        self._build_state = build_state
        self._on_switch_changed = on_switch_changed
        self._server: asyncio.AbstractServer | None = None
        self._writer: asyncio.StreamWriter | None = None
        self._peer: str = ""
        self._hello: HelloMsg | None = None

    # ---- lifecycle ----
    async def start(self) -> None:
        self._server = await asyncio.start_server(self._handle_client, self.host, self.port)
        log.info("switch server listening on %s:%d", self.host, self.port)

    async def stop(self) -> None:
        if self._writer is not None:
            try:
                self._writer.close()
                await self._writer.wait_closed()
            except Exception:
                pass
            self._writer = None
        if self._server is not None:
            self._server.close()
            try:
                await asyncio.wait_for(self._server.wait_closed(), timeout=2.0)
            except Exception:
                pass
            self._server = None

    def is_connected(self) -> bool:
        return self._writer is not None and not self._writer.is_closing()

    # ---- outbound ----
    async def _send(self, msg: Any) -> bool:
        w = self._writer
        if w is None or w.is_closing():
            return False
        try:
            w.write(protocol.encode(msg))
            await w.drain()
            return True
        except Exception as e:  # noqa: BLE001
            log.warning("switch send failed: %r", e)
            return False

    async def push_state(self) -> bool:
        if not self.is_connected():
            return False
        st = self._build_state()
        ok = await self._send(st)
        if ok:
            log.info("state seq=%d pushed to Switch (cups=0x%06x items=0x%06x speed=%d%%)",
                     st.seq, st.cups, st.items, st.speed_pct)
        return ok

    async def send_print(self, text: str) -> bool:
        return await self._send(PrintMsg(text=text))

    # ---- inbound ----
    async def _handle_client(self, reader: asyncio.StreamReader, writer: asyncio.StreamWriter) -> None:
        peer = writer.get_extra_info("peername")
        peer_ip = peer[0] if peer else "?"
        if self._writer is not None and not self._writer.is_closing():
            log.info("switch: replacing previous connection from %s with %s", self._peer, peer_ip)
            try:
                self._writer.close()
            except Exception:
                pass
        self._writer = writer
        self._peer = peer_ip
        self.state.switch_conn = "connected"
        self.state.switch_info = {"peer": peer_ip, "since": time.time()}
        self._notify()
        log.info("switch connected from %s", peer_ip)
        try:
            while True:
                try:
                    line = await reader.readline()
                except (ConnectionResetError, asyncio.IncompleteReadError):
                    break
                if not line:
                    break
                line = line.strip()
                if not line:
                    continue
                try:
                    msg = protocol.parse_line(line)
                except protocol.ProtocolError as e:
                    log.warning("switch: dropped bad line (%s): %r", e, line[:120])
                    continue
                try:
                    await self._dispatch(msg)
                except Exception:  # noqa: BLE001
                    log.exception("switch: handler error for %s", getattr(msg, "t", "?"))
        finally:
            if self._writer is writer:
                self._writer = None
                self.state.switch_conn = "disconnected"
                self._notify()
                log.info("switch disconnected (%s)", peer_ip)
            try:
                writer.close()
            except Exception:
                pass

    async def _dispatch(self, msg: Any) -> None:
        if isinstance(msg, HelloMsg):
            self._hello = msg
            self.state.switch_info.update({
                "mod_ver": msg.mod_ver, "game_ver": msg.game_ver, "build_id": msg.build_id,
                "resolved": msg.resolved, "total": msg.total,
                "unresolved": msg.unresolved,
            })
            log.info("switch HELLO mod=%s game=%s build=%s symbols=%d/%d",
                     msg.mod_ver, msg.game_ver, msg.build_id, msg.resolved, msg.total)
            if msg.game_ver and msg.game_ver != SUPPORTED_GAME_VERSION:
                log.warning("switch reports game version %s; this client targets %s",
                            msg.game_ver, SUPPORTED_GAME_VERSION)
            if msg.total and msg.resolved < msg.total:
                off = describe_unresolved(msg.unresolved)
                log.warning("switch located %d/%d game functions. Not found: %s. Affected: %s",
                            msg.resolved, msg.total, msg.unresolved or "(not reported)",
                            ", ".join(off) or "unknown")
            await self._send(HelloAckMsg(ok=True, seed=self.state.seed, slot=self.state.slot,
                                         client_ver=self.client_ver))
            await self.push_state()
            self._notify()
        elif isinstance(msg, RaceResultMsg):
            await self._on_race_result(msg)
        elif isinstance(msg, PlayReportMsg):
            await self._on_play_report(msg)
        elif isinstance(msg, CourseNameMsg):
            if self.state.learned.learn_course(msg.course_id, msg.res):
                log.info("learned course id %d -> %s", msg.course_id, msg.res)
        elif isinstance(msg, IdTableMsg):
            n = self.state.learned.learn_table(msg.kind, msg.entries)
            log.info("id_table %s: %d entries (%d new)", msg.kind, len(msg.entries), n)
            log.info("id_table %s entries: %s", msg.kind,
                     ", ".join(f"{e.get('id')}={e.get('text')}" for e in msg.entries))
            if n:
                await self.push_state()  # masks depend on the learned ids
        elif isinstance(msg, LogMsg):
            level = {"debug": logging.DEBUG, "info": logging.INFO, "warn": logging.WARNING,
                     "error": logging.ERROR}.get(msg.level, logging.INFO)
            log.log(level, "[switch:%s] %s", msg.level, msg.msg)
        elif isinstance(msg, PingMsg):
            await self._send(PongMsg(ts_ms=msg.ts_ms))
        else:
            await self._send(ErrMsg(code="unknown_type", ctx=str(getattr(msg, "t", "?"))))

    def _notify(self) -> None:
        if self._on_switch_changed is not None:
            try:
                self._on_switch_changed()
            except Exception:  # noqa: BLE001
                log.exception("on_switch_changed callback failed")
