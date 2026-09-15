import struct

import pytest

from client.play_report import DecodeError, decode_msgpack, parse_race_report


def _str(s: str) -> bytes:
    b = s.encode()
    return bytes([0xA0 | len(b)]) + b if len(b) < 32 else b"\xd9" + bytes([len(b)]) + b


def _map(pairs: list[tuple[str, bytes]]) -> bytes:
    return b"\xde" + len(pairs).to_bytes(2, "big") + b"".join(_str(k) + v for k, v in pairs)


def test_decode_scalars_and_nested():
    buf = _map([
        ("small", bytes([42])),
        ("u8", b"\xcc\xff"),
        ("u16", b"\xcd\x01\x00"),
        ("u32", b"\xce" + (70000).to_bytes(4, "big")),
        ("u64", b"\xcf" + (2**40).to_bytes(8, "big")),
        ("neg1", b"\xff"),
        ("s8", b"\xd0\x80"),
        ("s32", b"\xd2" + struct.pack(">i", -70000)),
        ("f64", b"\xcb" + struct.pack(">d", 1.5)),
        ("t", b"\xc3"), ("f", b"\xc2"), ("nil", b"\xc0"),
        ("arr", b"\x92" + bytes([1]) + _str("x")),
        ("obj", b"\x81" + _str("k") + bytes([7])),
        ("ext", b"\xd7\x00" + (99).to_bytes(8, "big")),
        ("long", b"\xd9\x21" + b"a" * 33),
    ])
    d = decode_msgpack(buf)
    assert d["small"] == 42 and d["u8"] == 255 and d["u16"] == 256 and d["u32"] == 70000
    assert d["u64"] == 2**40 and d["neg1"] == -1 and d["s8"] == -128 and d["s32"] == -70000
    assert d["f64"] == 1.5 and d["t"] is True and d["f"] is False and d["nil"] is None
    assert d["arr"] == [1, "x"] and d["obj"] == {"k": 7}
    assert d["ext"]["ext_type"] == 0 and d["long"] == "a" * 33


def test_truncated_raises():
    with pytest.raises(DecodeError):
        decode_msgpack(b"\xde\x00\x02" + _str("a") + bytes([1]))


def test_parse_race_report_fields():
    buf = _map([
        ("BeginTime", b"\xcf" + (20260915120000).to_bytes(8, "big")),
        ("EndTime", b"\xcf" + (20260915120300).to_bytes(8, "big")),
        ("RaceNo", bytes([1])),
        ("Course", _str("Gu_FirstCircuit")),
        ("Rank", bytes([2])),
        ("Points", bytes([12])),
        ("FinishReason", _str("Finish")),
        ("Mode", _str("Offline")),
        ("Rule", _str("VS")),
        ("Engine", _str("150cc")),
        ("Driver", _str("Kinopio")),
        ("Body", _str("K_Std")), ("Tire", _str("T_Std")), ("Wing", _str("W_Std")),
    ])
    rep = parse_race_report(buf)
    assert rep is not None
    assert rep.course == "Gu_FirstCircuit" and rep.rank == 2 and rep.points == 12
    assert rep.finished and rep.mode == "Offline" and rep.driver == "Kinopio"
    assert rep.end_time == "20260915120300" and "Gu_FirstCircuit" in rep.key


def test_parse_race_report_non_race():
    assert parse_race_report(_map([("Something", bytes([1]))])) is None
    assert parse_race_report(b"garbage") is None
