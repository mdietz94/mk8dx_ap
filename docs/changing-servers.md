# Changing servers, rooms or PCs

Short version: **you never re-run setup for this.** The mod carries no server,
room, slot or IP address.

| Change | What to do |
|---|---|
| New seed / room on the same server | Connect the client to the new room. |
| Different server (archipelago.gg, a friend's host, localhost) | Type the new address in the client's *Server* bar, Connect. |
| Different slot name | Connect with the new name. The client sends the new restriction state to the game immediately. |
| PC got a new LAN address | Nothing: the Switch sweeps its own subnet every time it reconnects. |
| Another PC runs the client | Install `mk8dx.apworld` there and open the client. The mod on the Switch is unchanged; skip the wizard's Install step or just let it report "mod installed". |
| Switch moved to another network | Nothing, as long as the PC running the client is on that network too. |
| New apworld release | Replace `mk8dx.apworld`, `/setup` > Install (the mod version is tied to the apworld version). |

## How it works

The Archipelago connection lives entirely in the client (PC <-> server). The
game only talks to the client, and finds it like this every time it
(re)connects:

1. UDP probe to `127.0.0.1:17779` (Ryujinx on the same PC answers at once).
2. UDP probe to every address of the console's own subnet (learned from the
   console at runtime), port 17779.
3. The client's reply names its TCP port (17778); the game connects there.

While the client is not connected to an AP server it tells the game
`enabled=false`, so the game plays vanilla until you connect.

## When discovery cannot work

Discovery needs the PC and the Switch on the same subnet. If they are not
(for example a PC on a wired VLAN and a Switch on a separate Wi-Fi subnet),
either put them on one network, or build the mod yourself with
`-DBRIDGE_HOST=<PC address>` (README, *Build*): that build also sweeps the
PC's /24.

The client's ports can be changed in `host.yaml` (`mk8dx_options:
switch_listen_port`), but the mod probes 17779 and expects the reply to name
the port, so only change the TCP port, and only if 17778 is taken.
