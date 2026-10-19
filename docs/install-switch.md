# Installing on a real Switch (Atmosphere)

The mod is an Atmosphere exefs patch, two files in
`sd:/atmosphere/contents/0100152000022000/exefs/`, plus one LayeredFS file,
`.../romfs/ap_config.json`, holding the PC's LAN address. The setup wizard writes them
for you ([first-time-setup.md](first-time-setup.md)); this page covers what is
specific to a console.

> **Status:** the mod has been verified under Ryujinx only. Real hardware has
> not been tested yet (discovery sweep, LayeredFS config read, socket pool
> on-console). The discovery scheme itself is the one dread_ap uses on real
> hardware. Reports
> welcome.

## Requirements

- A Switch running **Atmosphere** custom firmware. Start from the
  [NH Switch Guide](https://nh-server.github.io/switch-guide/) if you have none.
- Mario Kart 8 Deluxe with the **4.0.0** update installed on the console.
- The Switch and the PC running the MK8DX Client on the **same network**
  (same router / Wi-Fi; a guest network or "AP isolation" blocks it).

## Install

1. Put the SD card in the PC (or use a USB/MTP transfer tool).
2. Run the wizard (`/setup` in the client), choose **Switch SD card**, pick
   the drive, **Install**. With a transfer tool instead, choose **Folder** and
   copy the resulting `atmosphere` folder onto the card, merging with the
   existing one.
3. On the *Installed* page, click **Allow through Windows Firewall** unless
   Windows already asked you to allow the client and you said yes. Mark the
   PC's network as **Private** (Settings > Network & internet > your network).
4. Eject the card, boot Atmosphere, start Mario Kart 8 Deluxe.

The files land at:

```
sd:/atmosphere/contents/0100152000022000/exefs/subsdk9
sd:/atmosphere/contents/0100152000022000/exefs/main.npdm
sd:/atmosphere/contents/0100152000022000/romfs/ap_config.json
```

## How the Switch finds the PC

Nothing about your network is built into the mod. The wizard writes the PC's
LAN address into `romfs/ap_config.json`; when the game mounts its RomFS the
mod reads it, and then sends one small UDP probe (port 17779) to every address
in that /24 (for 192.168.1.20: 192.168.1.1 to .254). The MK8DX Client answers
with its current address and the Switch connects on TCP 17778. It retries with
backoff (1, 2, 5, 10, 30 s) until the client is running. So the PC can get a
different address on the same network, and you can change rooms or servers,
without reinstalling. Only moving the PC to a different network (another
/24) needs `/setup` > Install again.

## Do not play online with the mod

Atmosphere loads the mod whenever the game starts. Play Archipelago offline
(Grand Prix / VS against CPUs) and remove the mod before playing online:
`/setup` > **Switch SD card** > **Remove mod** (or delete the two files).
Removal restores any other mod the wizard replaced.

## Other MK8DX mods

Another exefs mod for the same title (a `subsdk9` or `main.npdm` in the same
folder) cannot run at the same time. The wizard keeps the other mod's files as
`*.mk8ap-backup` and **Remove mod** puts them back.

## Troubleshooting

- **Client never shows `Switch: connected`.** Check the firewall step and that
  the network is Private. Check the address in `romfs/ap_config.json` is on
  the Switch's network (System Settings > Internet shows the Switch's
  address; the first three numbers must match). A VPN on the PC can make the
  wizard detect the wrong adapter: correct the address and Install again. Mesh / travel routers that isolate wireless clients block
  the probe; try the PC on Ethernet to the same router.
- **The game boots but nothing happens.** The mod is inactive on any version
  other than 4.0.0. Check the update under *System Settings > Data
  Management* or the game's title screen version.
- **Debug log on console:** Atmosphere does not save the mod's log. Once
  connected, the client mirrors every Switch log line as `[switch:info]`. For
  problems before that, a developer build with `-DMK8AP_DEBUG_SD_LOG=ON`
  writes the boot log to `sd:/mk8dx_ap.txt`.
