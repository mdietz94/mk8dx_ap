"""Wire format for the Switch <-> MK8DX Client channel.

Mirror of switch-mod/src/ap/ApProtocol.hpp; spec in docs/wire-protocol.md.
One persistent TCP connection, one '\\n'-terminated UTF-8 JSON object per
message, field "t" is the type. Max line 8 KiB.
"""

from __future__ import annotations

import json
from dataclasses import asdict, dataclass, field
from typing import Any

MAX_LINE_BYTES = 8 * 1024


# ---------------------------------------------------------------------------
# Switch -> PC
# ---------------------------------------------------------------------------

@dataclass
class HelloMsg:
    t: str = "hello"
    mod_ver: str = ""
    game_ver: str = ""
    build_id: str = ""
    resolved: int = 0
    total: int = 0
    device_id: str = ""


@dataclass
class RaceResultMsg:
    """In-game hook path (ui::RaceWindow::onFakeGoal). rank is 0-based."""
    t: str = "race_result"
    seq: int = 0
    course_id: int = -1
    rule: int = -1
    engine: int = -1
    mirror: bool = False
    rank: int = -1
    kart_index: int = -1


@dataclass
class PlayReportMsg:
    """Serialized nn::prepo PlayReport (MessagePack, hex-encoded)."""
    t: str = "play_report"
    room: str = ""
    size: int = 0
    truncated: bool = False
    hex: str = ""

    def payload(self) -> bytes:
        return bytes.fromhex(self.hex) if self.hex else b""


@dataclass
class CourseNameMsg:
    t: str = "course_name"
    course_id: int = -1
    res: str = ""


@dataclass
class IdTableMsg:
    t: str = "id_table"
    kind: str = ""
    entries: list[dict] = field(default_factory=list)  # [{"id": int, "text": str}]


@dataclass
class LogMsg:
    t: str = "log"
    level: str = "info"
    msg: str = ""


@dataclass
class PingMsg:
    t: str = "ping"
    ts_ms: int = 0


# ---------------------------------------------------------------------------
# PC -> Switch
# ---------------------------------------------------------------------------

@dataclass
class HelloAckMsg:
    t: str = "hello_ack"
    ok: bool = True
    seed: str = ""
    slot: str = ""
    client_ver: str = ""
    err: str = ""


@dataclass
class StateMsg:
    """Full restriction snapshot. Idempotent; resent on every change and
    after every HELLO. Bit i of a mask == id i allowed."""
    t: str = "state"
    seq: int = 0
    enabled: bool = False
    cups: int = 0xFFFFFFFF
    drivers: list[int] = field(default_factory=lambda: [0xFFFFFFFF] * 4)
    bodies: list[int] = field(default_factory=lambda: [0xFFFFFFFF] * 2)
    tires: int = 0xFFFFFFFF
    wings: int = 0xFFFFFFFF
    items: int = 0xFFFFFFFF
    speed_pct: int = 100
    engine_max: int = 3
    def_driver: int = 0
    def_body: int = 0
    def_tire: int = 0
    def_wing: int = 0
    cpu_items: bool = False


@dataclass
class PrintMsg:
    t: str = "print"
    text: str = ""


@dataclass
class PongMsg:
    t: str = "pong"
    ts_ms: int = 0


@dataclass
class ErrMsg:
    t: str = "err"
    code: str = ""
    ctx: str = ""


# ---------------------------------------------------------------------------
# (de)serialization
# ---------------------------------------------------------------------------

_INBOUND: dict[str, type] = {
    "hello": HelloMsg,
    "race_result": RaceResultMsg,
    "play_report": PlayReportMsg,
    "course_name": CourseNameMsg,
    "id_table": IdTableMsg,
    "log": LogMsg,
    "ping": PingMsg,
}


class ProtocolError(ValueError):
    pass


def encode(msg: Any) -> bytes:
    """Dataclass -> one JSON line (bytes, trailing newline)."""
    line = json.dumps(asdict(msg), separators=(",", ":"), ensure_ascii=False) + "\n"
    data = line.encode("utf-8")
    if len(data) > MAX_LINE_BYTES:
        raise ProtocolError(f"message too long ({len(data)} bytes)")
    return data


def parse_line(line: bytes | str) -> Any:
    """One JSON line -> dataclass. Unknown fields are ignored; unknown
    types raise ProtocolError (callers log + drop)."""
    if isinstance(line, bytes):
        if len(line) > MAX_LINE_BYTES:
            raise ProtocolError("line too long")
        line = line.decode("utf-8", errors="replace")
    try:
        obj = json.loads(line)
    except json.JSONDecodeError as e:
        raise ProtocolError(f"malformed JSON: {e}") from e
    if not isinstance(obj, dict) or "t" not in obj:
        raise ProtocolError("missing type field")
    cls = _INBOUND.get(obj["t"])
    if cls is None:
        raise ProtocolError(f"unknown message type {obj['t']!r}")
    fields = {k: v for k, v in obj.items() if k in cls.__dataclass_fields__}
    try:
        return cls(**fields)
    except TypeError as e:
        raise ProtocolError(f"bad fields for {obj['t']}: {e}") from e
