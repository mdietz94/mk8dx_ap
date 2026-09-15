import asyncio
import json

import pytest

from client.protocol import PlayReportMsg, RaceResultMsg, StateMsg
from client.state import BridgeState
from client.switch_server import SwitchServer


@pytest.mark.asyncio
async def test_hello_ack_state_and_dispatch():
    state = BridgeState()
    state.slot, state.seed = "Mario", "seed123"
    got_results: list = []
    got_reports: list = []

    async def on_rr(m: RaceResultMsg):
        got_results.append(m)

    async def on_pr(m: PlayReportMsg):
        got_reports.append(m)

    sw = SwitchServer("127.0.0.1", 0, state, client_ver="t", on_race_result=on_rr,
                      on_play_report=on_pr, build_state=state.build_state)
    await sw.start()
    port = sw._server.sockets[0].getsockname()[1]
    reader, writer = await asyncio.open_connection("127.0.0.1", port)

    async def send(obj):
        writer.write((json.dumps(obj) + "\n").encode())
        await writer.drain()

    async def recv():
        return json.loads(await asyncio.wait_for(reader.readline(), 3))

    await send({"t": "hello", "mod_ver": "x", "game_ver": "3.0.5", "build_id": "FE", "resolved": 1, "total": 2})
    ack = await recv()
    assert ack["t"] == "hello_ack" and ack["ok"] and ack["slot"] == "Mario" and ack["seed"] == "seed123"
    st = await recv()
    assert st["t"] == "state" and st["cups"] == 1
    assert state.switch_info["resolved"] == 1

    await send({"t": "race_result", "seq": 1, "course_id": 3, "rule": 1, "engine": 2, "mirror": False, "rank": 0, "kart_index": 0})
    await send({"t": "play_report", "room": "R", "size": 3, "truncated": False, "hex": "de0000"})
    await send({"t": "course_name", "course_id": 3, "res": "tc_Course_Gu_Cake"})
    await send({"t": "id_table", "kind": "driver", "entries": [{"id": 9, "text": "Luigi"}]})
    st2 = await recv()  # id_table triggers a state push
    assert st2["t"] == "state" and st2["seq"] == 2
    await send({"t": "ping", "ts_ms": 77})
    pong = await recv()
    assert pong["t"] == "pong" and pong["ts_ms"] == 77
    await send({"t": "bogus"})  # unknown type: dropped by parser, no crash
    await send({"t": "ping", "ts_ms": 78})
    assert (await recv())["ts_ms"] == 78

    assert len(got_results) == 1 and got_results[0].course_id == 3
    assert len(got_reports) == 1 and got_reports[0].payload() == b"\xde\x00\x00"
    assert state.learned.course_for_id(3).name == "Sweet Sweet Canyon"
    assert state.learned.maps["driver"].id_for("Luigi") == 9

    writer.close()
    await sw.stop()
    assert state.switch_conn == "disconnected"
