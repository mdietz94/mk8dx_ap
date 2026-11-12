# Wire protocol: Switch mod <-> MK8DX Client

One persistent TCP connection (Switch dials the PC). Each message is one
`\n`-terminated UTF-8 JSON object; field `t` is the type. Max line 8 KiB.
Unknown fields are ignored on both sides; unknown types are logged and
dropped. C++ mirror: `switch-mod/src/ap/ApProtocol.hpp`; Python mirror:
`apworld/mk8dx_archipelago/client/protocol.py`.

Discovery (UDP, port 17779): the Switch probes `127.0.0.1` and then every
host of the /24 of `bridge_host` from `<romfs>:/ap_config.json` (written next
to the mod by the setup wizard, read once when the game mounts its RomFS)
with `{"t":"discover","mod_ver":..}`; the client answers
`{"t":"bridge","host":<ipv4>,"port":17778,"seed":..}`. Nothing
network-specific is compiled in, so one binary serves every player.

## Switch -> PC

| `t` | fields | when |
|---|---|---|
| `hello` | `mod_ver, game_ver, build_id, resolved, total, unresolved, device_id` | on every (re)connect; `unresolved` is a comma-separated list of game functions the mod could not locate |
| `play_report` | `room, size, truncated, hex` | every nn::prepo PlayReport the game saves (end-of-race records carry Course/Rank/Rule/Engine/Driver/...); `hex` is MessagePack |
| `race_result` | `seq, course_id, rule, engine, mirror, rank(0-based), kart_index` | `ui::RaceWindow::onFakeGoal` (fallback channel) |
| `gp_result` | `seq, cup(0-based), engine, placing(1-based), points` | a Grand Prix ended (`GpResultHook`, where the game records the trophy). The client credits `Complete <cup> (Grand Prix)` for any placing and `Win <cup> (Grand Prix)` for `placing` 1 (a 0.3.0 seed, without `gp_win_locations` in slot data, has only `Complete`, credited for `placing` 1) |
| `course_name` | `course_id, res` | accepted by the client but not sent by the mod (`ui::CourseInfo` carries no name). Course ids are learned by pairing `race_result` with the `play_report` of the same race |
| `id_table` | `kind ("driver"/"body"/"tire"/"wing"), entries:[{id,text}]` | once per session after save load |
| `log` | `level, msg` | every INFO+ log line |
| `ping` | `ts_ms` | liveness |

## PC -> Switch

| `t` | fields | when |
|---|---|---|
| `hello_ack` | `ok, seed, slot, client_ver, err` | reply to `hello` |
| `state` | `seq, enabled, cups, drivers[4], bodies[2], tires, wings, items, speed_pct, stat_cap, base_speed_pct, engine_max, def_driver, def_body, def_tire, def_wing, cpu_items` | after `hello_ack`, after every ReceivedItems, after new id tables |
| `print` | `text` | free text for the Switch log |
| `pong` | `ts_ms` | reply to `ping` |
| `err` | `code, ctx` | protocol errors |

`state` is a full idempotent snapshot: bit `i` of a mask means id `i` is
allowed. Masks are indexed by the game's own enum ids (learned at runtime,
see `client/learned.py`). `enabled=false` (or no state received yet) means
the game plays vanilla. There is no item replay and nothing to double-count.

`speed_pct` (1..100) drives the per-frame top-speed governor
(`speed_restriction: top_speed_cap`). In `speed_restriction: stat_cap`
mode, `stat_cap` (1..20, 20 = none) caps every stat sum of the local kart
at race build and `base_speed_pct` (1..100) scales its base top speeds at
the same moment. The client sends the unused fields at their neutral
values (100 / 20 / 100). A mod older than 0.3.0 skips both new keys.
