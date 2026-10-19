# First-time setup

Everything a new player does once per PC. Afterwards, joining a multiworld is
the same as any other Archipelago game: open **MK8DX Client** from the
Archipelago Launcher and connect.

Time: about five minutes. Nothing is compiled and no game files are read:
the Switch mod ships ready-made inside `mk8dx.apworld`. Setup copies it and
writes this PC's LAN address next to it, which the Switch uses to find the
client (see [changing servers](changing-servers.md)).

## You need

| | |
|---|---|
| **Mario Kart 8 Deluxe, update 4.0.0** | The mod checks the game's build at boot and does nothing on any other version. 4.0.0 is the current (final) update. |
| **Ryujinx** on this PC, **or** a Switch with **Atmosphere** | Ryujinx 1.3.x is what the mod is tested on. For a Switch, see [install-switch.md](install-switch.md). |
| **Archipelago 0.6 or newer** | <https://github.com/ArchipelagoMW/Archipelago/releases> |
| The PC and the Switch on the **same network** | Not needed for Ryujinx on the same PC. |

The Booster Course Pass is optional; generate with
`course_selection: base_game` if your save does not own it.

## Steps

1. **Download `mk8dx.apworld`** from the
   [releases page](https://github.com/mdietz94/mk8dx_ap/releases) and put it in
   Archipelago's `custom_worlds` folder (Windows default:
   `C:\ProgramData\Archipelago\custom_worlds\`), or use *Install APWorld*
   in the Archipelago Launcher. Optional: check it against
   `mk8dx.apworld.sha256` from the same release.
2. **Open the Archipelago Launcher and click MK8DX Client.** On the first run
   the setup wizard opens next to the client by itself. (Later, type `/setup`
   in the client's command bar to open it again.)
3. **Walk the wizard**:
   1. *Welcome* lists the requirements and shows `Bundled mod: <version>
      (verified)`: the mod files were checked against the SHA-256 manifest
      inside the apworld.
   2. *Where does the game run?*
      - **Ryujinx**: the data folder is filled in for you
        (`%APPDATA%\Ryujinx`). For a portable Ryujinx, browse to the folder
        that holds `Ryujinx.exe` (it has a `portable` subfolder).
      - **Switch SD card**: pick the card's drive; drives with an
        `atmosphere` folder are listed as buttons.
      - **Folder**: any folder; the wizard writes the Atmosphere layout in it
        for you to copy to the SD card later (DBI, FTP, a card reader on
        another PC).

      **This PC's address** is filled in automatically: it goes into
      `romfs/ap_config.json` next to the mod, and the Switch searches that
      address's network (its /24) for the client. Change it only if the PC has
      several network adapters (VPN, WSL, virtual machines) and the detected
      one is not on the Switch's network. Ryujinx on the same PC does not use
      it.

      The page shows where the files go and what is there now (*not
      installed*, *mod X installed, PC address ...*, or *differs from the
      bundled mod*).
      Click **Install**. If another MK8DX mod's `subsdk9` / `main.npdm` is
      already there, it is kept as `subsdk9.mk8ap-backup` /
      `main.npdm.mk8ap-backup` and **Remove mod** puts it back.
   3. *Installed* says what to do next. For a Switch or a folder it also
      offers **Allow through Windows Firewall**: one admin prompt adds
      inbound rules for TCP 17778 and UDP 17779 (Private networks). Skip it
      if Windows already asked you to allow the client and you said yes.
4. **Start the game.** Ryujinx: close Mario Kart completely and start it again
   (mods load at game start). Switch: put the SD card back, boot Atmosphere,
   start Mario Kart.
5. **Connect.** In the client, enter your room's address and slot name and
   click **Connect**. The *Kart* tab shows `Switch: connected` and
   `Mod: <version> on <target>` once the game reaches the title screen.

## Afterwards

- **New seed, new room, new slot:** nothing to redo. Just connect.
- **New apworld release:** replace `mk8dx.apworld`, open the client and type
  `/setup`, click **Install**. The client warns in its log (and the *Kart* tab
  says so) when the installed mod is not the bundled one, and when the Switch
  reports an older mod in its HELLO.
- **Switching between Ryujinx and a Switch:** run `/setup` and pick the other
  target. Both can stay installed.
- **The PC moved to another network** (another router, a different subnet):
  `/setup` > Install again so `ap_config.json` has the new address. The
  client warns in its log when it notices this.

## Without the wizard

The same steps from a terminal, for headless setups. Inside Archipelago's
folder (`ArchipelagoLauncher.exe` installs ship no `python`, so this is for a
source install of Archipelago):

```
python -m worlds.mk8dx._setup.cli status
python -m worlds.mk8dx._setup.cli deploy ryujinx
python -m worlds.mk8dx._setup.cli deploy sd --root E:/ --bridge-host 192.168.1.20
python -m worlds.mk8dx._setup.cli remove sd --root E:/
python -m worlds.mk8dx._setup.cli firewall
```

## Troubleshooting

**The client says `Switch: not connected`.**
- Is the game on update 4.0.0? Ryujinx's log (or the client's log once
  connected) prints the build id; anything but `2C336A9B...` leaves the mod
  inactive.
- Ryujinx: did you restart the game after installing? Ryujinx's log
  (`Logs\Ryujinx_*.log` next to Ryujinx.exe, or `%APPDATA%\Ryujinx\Logs`)
  should contain `mk8dx_archipelago <version> hkMain START`.
- Switch: same network as the PC? Firewall rules added (step 3.3)? Is the PC's
  network marked **Private** in Windows (the rules apply to Private and
  Domain networks only)? See [install-switch.md](install-switch.md) for the
  on-console checks.

**The wizard says "this apworld has no bundled Switch mod".** You have a
development build of the apworld. Download the release, or build the mod
yourself (README, *Build*) and use `scripts/deploy_mod.py`.

**"does not match its manifest SHA-256".** The download is damaged or the zip
was edited. Download `mk8dx.apworld` again.

**The wizard window does not appear.** It writes a log to
`%APPDATA%\MK8DXArchipelago\wizard.log`, and the client's own log is in
Archipelago's `logs` folder (`MK8DXClient_*.txt`).

## What lives where

| Path | What |
|---|---|
| `%APPDATA%\MK8DXArchipelago\setup_state.json` | last target, its folder, installed mod version |
| `%APPDATA%\MK8DXArchipelago\wizard.log` | wizard log |
| `%APPDATA%\Ryujinx\mods\contents\0100152000022000\exefs\` | Ryujinx install |
| `<SD>:\atmosphere\contents\0100152000022000\exefs\` | Switch install |
| `...\0100152000022000\romfs\ap_config.json` | the PC address the Switch searches (next to each install) |
| `<Archipelago user dir>\mk8dx_learned_ids.json` | course ids the client learned from your races |

To start over, delete `%APPDATA%\MK8DXArchipelago\`; the wizard opens again on
the next client start.
