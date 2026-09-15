"""nn::prepo PlayReport decoder.

The Switch mod forwards the serialized report the game hands to the prepo
service. Nintendo's encoding is MessagePack (Ryujinx decodes it with the
same library): 0xDE = map16 header, fixstr keys, and a mix of fixint /
uint8..64 / int8..64 / str / bool / float / nested map / array values.
The smbw_ap project reverse-engineered the same format independently
(their "CBOR-ish" table is the msgpack opcode table).

MK8DX end-of-race reports (as seen in Ryujinx "PlayReport log:" lines and
in mk8-stats-bridge) carry, among others:

    BeginTime, EndTime (yyyymmddHHMMSS), RaceNo, Course, Rank, Points,
    FinishReason ("Finish"/...), Mode ("Offline"/"Local"/...), Rule,
    Engine ("150cc"...), Driver, Body, Tire, Wing

Only the subset needed here is exposed; the full decoded dict is returned
for logging and future use.
"""

from __future__ import annotations

import struct
from dataclasses import dataclass, field
from typing import Any


class DecodeError(ValueError):
    pass


class _Reader:
    __slots__ = ("buf", "pos")

    def __init__(self, buf: bytes):
        self.buf = buf
        self.pos = 0

    def need(self, n: int) -> None:
        if self.pos + n > len(self.buf):
            raise DecodeError(f"need {n} bytes at {self.pos}, have {len(self.buf) - self.pos}")

    def u8(self) -> int:
        self.need(1)
        v = self.buf[self.pos]
        self.pos += 1
        return v

    def take(self, n: int) -> bytes:
        self.need(n)
        v = self.buf[self.pos:self.pos + n]
        self.pos += n
        return v

    def unpack(self, fmt: str) -> Any:
        n = struct.calcsize(fmt)
        return struct.unpack(fmt, self.take(n))[0]


def _read_str(r: _Reader, n: int) -> str:
    return r.take(n).decode("utf-8", errors="replace")


def _value(r: _Reader) -> Any:
    op = r.u8()
    if op <= 0x7F:
        return op
    if 0x80 <= op <= 0x8F:
        return {_value(r): _value(r) for _ in range(op & 0x0F)}
    if 0x90 <= op <= 0x9F:
        return [_value(r) for _ in range(op & 0x0F)]
    if 0xA0 <= op <= 0xBF:
        return _read_str(r, op & 0x1F)
    if op == 0xC0:
        return None
    if op == 0xC2:
        return False
    if op == 0xC3:
        return True
    if op == 0xC4:
        return r.take(r.u8())
    if op == 0xC5:
        return r.take(r.unpack(">H"))
    if op == 0xC6:
        return r.take(r.unpack(">I"))
    if op == 0xCA:
        return r.unpack(">f")
    if op == 0xCB:
        return r.unpack(">d")
    if op == 0xCC:
        return r.u8()
    if op == 0xCD:
        return r.unpack(">H")
    if op == 0xCE:
        return r.unpack(">I")
    if op == 0xCF:
        return r.unpack(">Q")
    if op == 0xD0:
        return r.unpack(">b")
    if op == 0xD1:
        return r.unpack(">h")
    if op == 0xD2:
        return r.unpack(">i")
    if op == 0xD3:
        return r.unpack(">q")
    if 0xD4 <= op <= 0xD8:  # fixext 1/2/4/8/16
        n = 1 << (op - 0xD4)
        type_code = r.unpack(">b")
        data = r.take(n)
        return {"ext_type": type_code, "ext_data": data.hex()}
    if op == 0xD9:
        return _read_str(r, r.u8())
    if op == 0xDA:
        return _read_str(r, r.unpack(">H"))
    if op == 0xDB:
        return _read_str(r, r.unpack(">I"))
    if op == 0xDC:
        return [_value(r) for _ in range(r.unpack(">H"))]
    if op == 0xDD:
        return [_value(r) for _ in range(r.unpack(">I"))]
    if op == 0xDE:
        return {_value(r): _value(r) for _ in range(r.unpack(">H"))}
    if op == 0xDF:
        return {_value(r): _value(r) for _ in range(r.unpack(">I"))}
    if op >= 0xE0:
        return op - 0x100
    raise DecodeError(f"unsupported msgpack opcode 0x{op:02x} at {r.pos - 1}")


def decode_msgpack(buf: bytes) -> Any:
    r = _Reader(buf)
    v = _value(r)
    return v


@dataclass
class RaceReport:
    """The fields an AP check needs, pulled out of a decoded report."""
    course: str = ""          # internal course name as the game reports it
    rank: int = 0             # 1-based in PlayReports
    points: int = 0
    finish_reason: str = ""
    mode: str = ""
    rule: str = ""
    engine: str = ""
    driver: str = ""
    body: str = ""
    tire: str = ""
    wing: str = ""
    race_no: int = 0
    begin_time: str = ""
    end_time: str = ""
    raw: dict = field(default_factory=dict)

    @property
    def finished(self) -> bool:
        return (not self.finish_reason) or self.finish_reason == "Finish"

    @property
    def key(self) -> str:
        """Dedupe key: one report per (course, end time)."""
        return f"{self.course}|{self.end_time or self.begin_time}|{self.race_no}"


def _get(d: dict, *names: str, default: Any = "") -> Any:
    for n in names:
        if n in d:
            return d[n]
    lower = {str(k).lower(): v for k, v in d.items()}
    for n in names:
        if n.lower() in lower:
            return lower[n.lower()]
    return default


def parse_race_report(buf: bytes) -> RaceReport | None:
    """Decode a report and return a RaceReport when it looks like an
    end-of-race record (has Course + Rank), else None."""
    try:
        obj = decode_msgpack(buf)
    except DecodeError:
        return None
    if not isinstance(obj, dict):
        return None
    course = _get(obj, "Course")
    rank = _get(obj, "Rank", default=None)
    if not course or rank is None:
        return None
    try:
        rank_i = int(rank)
    except (TypeError, ValueError):
        return None
    return RaceReport(
        course=str(course),
        rank=rank_i,
        points=int(_get(obj, "Points", default=0) or 0),
        finish_reason=str(_get(obj, "FinishReason", default="")),
        mode=str(_get(obj, "Mode", default="")),
        rule=str(_get(obj, "Rule", default="")),
        engine=str(_get(obj, "Engine", default="")),
        driver=str(_get(obj, "Driver", default="")),
        body=str(_get(obj, "Body", default="")),
        tire=str(_get(obj, "Tire", default="")),
        wing=str(_get(obj, "Wing", default="")),
        race_no=int(_get(obj, "RaceNo", default=0) or 0),
        begin_time=str(_get(obj, "BeginTime", default="")),
        end_time=str(_get(obj, "EndTime", default="")),
        raw=obj,
    )
