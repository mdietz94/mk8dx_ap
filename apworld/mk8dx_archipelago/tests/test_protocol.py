import json

import pytest

from client import protocol
from client.protocol import HelloMsg, PlayReportMsg, RaceResultMsg, StateMsg, encode, parse_line


def test_parse_hello_ignores_unknown_fields():
    m = parse_line(b'{"t":"hello","mod_ver":"0.1.0","game_ver":"3.0.5","build_id":"FE94","resolved":3,"total":4,"future":1}\n')
    assert isinstance(m, HelloMsg)
    assert m.resolved == 3 and m.build_id == "FE94"


def test_parse_race_result_and_play_report():
    m = parse_line('{"t":"race_result","seq":2,"course_id":7,"rule":1,"engine":2,"mirror":false,"rank":0,"kart_index":0}')
    assert isinstance(m, RaceResultMsg) and m.course_id == 7 and m.rank == 0
    pr = parse_line('{"t":"play_report","room":"R","size":3,"truncated":false,"hex":"de0001"}')
    assert isinstance(pr, PlayReportMsg) and pr.payload() == b"\xde\x00\x01"


@pytest.mark.parametrize("bad", [b"", b"not json", b'{"x":1}', b'{"t":"nope"}', b'[1,2]'])
def test_parse_rejects(bad):
    with pytest.raises(protocol.ProtocolError):
        parse_line(bad)


def test_encode_state_roundtrip_shape():
    st = StateMsg(seq=5, enabled=True, cups=0b101, drivers=[1, 2, 3, 4], bodies=[5, 6],
                  tires=7, wings=8, items=9, speed_pct=60)
    line = encode(st)
    assert line.endswith(b"\n")
    obj = json.loads(line)
    assert obj["t"] == "state" and obj["drivers"] == [1, 2, 3, 4] and obj["speed_pct"] == 60
    assert len(line) < protocol.MAX_LINE_BYTES


def test_encode_too_long():
    with pytest.raises(protocol.ProtocolError):
        encode(protocol.PrintMsg(text="x" * 9000))
