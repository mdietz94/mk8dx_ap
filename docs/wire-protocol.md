# Wire protocol: Switch mod <-> MK8DX Client

One persistent TCP connection (Switch dials the PC). Each message is one
`\n`-terminated UTF-8 JSON object; field `t` is the type. Max line 8 KiB.
Unknown fields are ignored on both sides; unknown types are logged and
dropped. C++ mirror: `switch-mod/src/ap/ApProtocol.hpp`; Python mirror:
`apworld/mk8dx_archipelago/client/protocol.py`.

Discovery (UDP, port 17779): the Switch probes `127.0.0.1` and then every
host of its own subnet (address and mask from `nn::nifm`, falling back to
the local address of a connected UDP socket; masks wider than /24 are
narrowed to the console's /24) with `{"t":"discover","mod_ver":..}`; the
client answers `{"t":"bridge","host":<ipv4>,"port":17778,"seed":..}`. A dev
build configured with `-DBRIDGE_HOST=<ip>` also sweeps that IP's /24.
Nothing network-specific is compiled in, so one binary serves every player.

## Switch -> PC

| `t` | fields | when |
|---|---|---|
| `hello` | `mod_ver, game_ver, build_id, resolved, total, unresolved, device_id` | on every (re)connect; `unresolved` is a comma-separated list of game functions the mod could not locate |
| `play_report` | `room, size, truncated, hex` | every nn::prepo PlayReport the game saves (end-of-race records carry Course/Rank/Rule/Engine/Driver/...); `hex` is MessagePack |
| `race_result` | `seq, course_id, rule, engine, mirror, rank(0-based), kart_index` | `ui::RaceWindow::onFakeGoal` (fallback channel) |
| `course_name` | `course_id, res` | accepted by the client but not sent by the mod (`ui::CourseInfo` carries no name). Course ids are learned by pairing `race_result` with the `play_report` of the same race |
| `id_table` | `kind ("driver"/"body"/"tire"/"wing"), entries:[{id,text}]` | once per session after save load |
| `log` | `level, msg` | every INFO+ log line |
| `ping` | `ts_ms` | liveness |

## PC -> Switch

| `t` | fields | when |
|---|---|---|
| `hello_ack` | `ok, seed, slot, client_ver, err` | reply to `hello` |
| `state` | `seq, enabled, cups, drivers[4], bodies[2], tires, wings, items, speed_pct, engine_max, def_driver, def_body, def_tire, def_wing, cpu_items` | after `hello_ack`, after every ReceivedItems, after new id tables |
| `print` | `text` | free text for the Switch log |
| `pong` | `ts_ms` | reply to `ping` |
| `err` | `code, ctx` | protocol errors |

`state` is a full idempotent snapshot: bit `i` of a mask means id `i` is
allowed. Masks are indexed by the game's own enum ids (learned at runtime,
see `client/learned.py`). `enabled=false` (or no state received yet) means
the game plays vanilla. There is no item replay and nothing to double-count.
