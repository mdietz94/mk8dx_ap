"""Fake Switch: drives the MK8DX Client's SwitchServer over loopback.

Run the client first (Launcher -> "MK8DX Client", connected to an AP room
with a Mario Kart 8 Deluxe slot), then:

    python scripts/switch_smoke_test.py [--port 17778] [--course Gu_FirstCircuit --rank 1]

Sends HELLO, an id_table for drivers, a course_name, then a synthetic
PlayReport race record for --course / --rank and prints everything the
client sends back (hello_ack, state pushes). Stop with Ctrl-C.
"""

from __future__ import annotations

import argparse
import asyncio
import json
import sys

sys.path.insert(0, str(__import__("pathlib").Path(__file__).resolve().parent.parent
                       / "apworld" / "mk8dx_archipelago" / "client"))


def msgpack_map(d: dict) -> bytes:
    """Minimal MessagePack encoder for str/int/bool maps (what PlayReports use)."""
    def enc(v) -> bytes:
        if isinstance(v, bool):
            return b"\xc3" if v else b"\xc2"
        if isinstance(v, int):
            if 0 <= v <= 0x7F:
                return bytes([v])
            if 0 <= v <= 0xFF:
                return b"\xcc" + bytes([v])
            if 0 <= v <= 0xFFFF:
                return b"\xcd" + v.to_bytes(2, "big")
            if 0 <= v <= 0xFFFFFFFF:
                return b"\xce" + v.to_bytes(4, "big")
            return b"\xcf" + v.to_bytes(8, "big")
        if isinstance(v, str):
            b = v.encode("utf-8")
            if len(b) < 32:
                return bytes([0xA0 | len(b)]) + b
            return b"\xd9" + bytes([len(b)]) + b
        if isinstance(v, dict):
            return msgpack_map(v)
        raise TypeError(type(v))
    out = b"\xde" + len(d).to_bytes(2, "big")
    for k, v in d.items():
        out += enc(str(k)) + enc(v)
    return out


async def reader_loop(reader: asyncio.StreamReader) -> None:
    while True:
        line = await reader.readline()
        if not line:
            print("[fake-switch] EOF")
            return
        try:
            print("<<", json.loads(line))
        except Exception:
            print("<< (raw)", line[:200])


async def main(port: int, course: str, rank: int, rule: str) -> int:
    reader, writer = await asyncio.open_connection("127.0.0.1", port)
    asyncio.create_task(reader_loop(reader))

    async def send(obj: dict) -> None:
        writer.write((json.dumps(obj) + "\n").encode("utf-8"))
        await writer.drain()
        print(">>", obj)

    await send({"t": "hello", "mod_ver": "fake-0.1.0", "game_ver": "3.0.5",
                "build_id": "FE941ED5BA14BE5D505698DA1BBF4FE7", "resolved": 17, "total": 18})
    await asyncio.sleep(1.0)
    await send({"t": "id_table", "kind": "driver",
                "entries": [{"id": 0, "text": "Mario"}, {"id": 1, "text": "Luigi"}, {"id": 5, "text": "Kinopio"}]})
    await send({"t": "course_name", "course_id": 0, "res": f"tc_Course_{course}"})
    await asyncio.sleep(0.5)
    report = {"BeginTime": 20260915120000, "EndTime": 20260915120300, "RaceNo": 1,
              "Course": course, "Rank": rank, "Points": 15, "FinishReason": "Finish",
              "Mode": "Offline", "Rule": rule, "Engine": "150cc",
              "Driver": "Mario", "Body": "K_Std", "Tire": "T_Std", "Wing": "W_Std"}
    payload = msgpack_map(report)
    await send({"t": "play_report", "room": "RaceResult", "size": len(payload),
                "truncated": False, "hex": payload.hex()})
    rule_id = {"GrandPrix": 0, "VS": 1, "TimeAttack": 2, "Battle": 3}.get(rule, 1)
    await send({"t": "race_result", "seq": 1, "course_id": 0, "rule": rule_id, "engine": 2,
                "mirror": False, "rank": rank - 1, "kart_index": 0})
    await send({"t": "log", "level": "info", "msg": "fake switch says hi"})
    await asyncio.sleep(2.0)
    await send({"t": "ping", "ts_ms": 1234})
    await asyncio.sleep(2.0)
    writer.close()
    return 0


if __name__ == "__main__":
    p = argparse.ArgumentParser()
    p.add_argument("--port", type=int, default=17778)
    p.add_argument("--course", default="Gu_FirstCircuit")
    p.add_argument("--rank", type=int, default=1)
    p.add_argument("--rule", default="VS")
    a = p.parse_args()
    try:
        sys.exit(asyncio.run(main(a.port, a.course, a.rank, a.rule)))
    except ConnectionRefusedError:
        print(f"connection refused: is the MK8DX Client listening on 127.0.0.1:{a.port}?")
        sys.exit(2)
    except KeyboardInterrupt:
        sys.exit(130)
