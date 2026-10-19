# Changing servers, rooms or PCs

Short version: **you do not re-run setup for this.** The mod carries no
server, room or slot. The only network fact setup records is this PC's LAN
address (in `romfs/ap_config.json`), and only its network (/24) matters.

| Change | What to do |
|---|---|
| New seed / room on the same server | Connect the client to the new room. |
| Different server (archipelago.gg, a friend's host, localhost) | Type the new address in the client's *Server* bar, Connect. |
| Different slot name | Connect with the new name. The client sends the new restriction state to the game immediately. |
| PC got a new LAN address on the same network | Nothing: the Switch sweeps the whole /24 every time it reconnects. |
| PC moved to a different network / subnet | `/setup` > Install again (records the new address). The client's log warns about this. |
| Another PC on the same network runs the client | Install `mk8dx.apworld` there and open the client. Nothing to redo on the Switch. |
| Another PC on a different network | Run the wizard on that PC and Install to the SD card (records its address). |
| Switch and PC moved to another network (e.g. a friend's house) | `/setup` > Install again, unless the PC's new address starts with the same three numbers as the one recorded. |
| New apworld release | Replace `mk8dx.apworld`, `/setup` > Install (the mod version is tied to the apworld version). |

## How it works

The Archipelago connection lives entirely in the client (PC <-> server). The
game only talks to the client, and finds it like this every time it
(re)connects:

1. UDP probe to `127.0.0.1:17779` (Ryujinx on the same PC answers at once).
2. UDP probe to every address of the /24 of the PC address in
   `ap_config.json` (written at setup, read when the game mounts its RomFS),
   port 17779.
3. The client's reply names its TCP port (17778); the game connects there.

While the client is not connected to an AP server it tells the game
`enabled=false`, so the game plays vanilla until you connect.

## When discovery cannot work

The Switch sweeps the PC address's /24 and replies must reach it, so the two
need to be on one routable network without client isolation. A PC on a
wired VLAN and a Switch on a separate Wi-Fi subnet can still work if the
router routes between them: the sweep targets the PC's /24, not the
Switch's. Guest networks and "AP isolation" block it.

The client's ports can be changed in `host.yaml` (`mk8dx_options:
switch_listen_port`), but the mod probes 17779 and expects the reply to name
the port, so only change the TCP port, and only if 17778 is taken.
