"""Kivy setup wizard: install the bundled Switch mod.

Opened in its own window by `/setup` in the MK8DX Client, and automatically
the first time the client starts on a machine. Three pages:

  1. Welcome  — requirements, and the bundled mod's version (verified
                against its manifest before anything else happens).
  2. Install  — pick Ryujinx / SD card / folder and confirm this PC's LAN
                address (written to ap_config.json next to the mod; the
                Switch sweeps its /24), see what is installed there now,
                Install (or Remove).
  3. Done     — what to do next for that target; for a real Switch, the
                Windows Firewall step.

Everything it does is in `deploy.py` / `firewall.py` / `state.py`; `cli.py`
is the same flow without a window.
"""

from __future__ import annotations

import time
import traceback
from pathlib import Path
from typing import Any

from . import GAME_VERSION, deploy, firewall, state, wizard_log_path
from .bundle import BundleError, ModBundle, load_bundled

NEXT_STEPS = {
    "ryujinx": ("Close Mario Kart 8 Deluxe completely if it is running, then start it again "
                "from Ryujinx (mods load when the game starts). Make sure the 4.0.1 update is "
                "installed (right-click the game > Manage Title Updates)."),
    "sd": ("Eject the SD card, put it back in the Switch and boot Atmosphere. Start Mario Kart "
           "8 Deluxe (update 4.0.1). The Switch and this PC must be on the same network."),
    "folder": ("Copy the 'atmosphere' folder from the chosen folder to the root of the Switch's "
               "SD card (merge with the existing one), then boot Mario Kart 8 Deluxe (update "
               "4.0.1) under Atmosphere. The Switch and this PC must be on the same network."),
}
CONNECT_TEXT = ("Then open the MK8DX Client from the Archipelago Launcher and connect to your room. "
                "The client shows 'Switch: connected' once the game reaches the title screen.")


def wizard_log(line: str) -> None:
    try:
        with open(wizard_log_path(), "a", encoding="utf-8") as f:
            f.write(f"[{time.strftime('%Y-%m-%d %H:%M:%S')}] {line}\n")
    except OSError:
        pass


def _ask_directory(title: str, initial: str | None = None) -> str:
    """Native folder picker (Tk, stdlib). "" on cancel or failure."""
    try:
        import tkinter
        import tkinter.filedialog
        root = tkinter.Tk()
        root.withdraw()
        root.attributes("-topmost", True)
        kw: dict[str, Any] = {"title": title, "parent": root}
        if initial and Path(initial).is_dir():
            kw["initialdir"] = initial
        chosen = tkinter.filedialog.askdirectory(**kw)
        root.destroy()
        return chosen or ""
    except Exception as e:  # noqa: BLE001
        wizard_log(f"folder picker failed: {e!r}")
        return ""


def run_wizard() -> None:
    """Open the wizard window; returns when it is closed."""
    import kvui  # noqa: F401  (must precede kivy.*: sets Kivy's data dir in frozen AP builds)

    from kivy.app import App
    from kivy.core.window import Window
    from kivy.metrics import dp
    from kivy.uix.boxlayout import BoxLayout
    from kivy.uix.button import Button
    from kivy.uix.label import Label
    from kivy.uix.screenmanager import Screen, ScreenManager
    from kivy.uix.textinput import TextInput
    from kivy.uix.togglebutton import ToggleButton
    from kivy.uix.widget import Widget

    wizard_log("=== wizard start ===")
    saved = state.load()
    bundle: ModBundle | None = None
    bundle_error = ""
    try:
        bundle = load_bundled()
    except BundleError as e:
        bundle_error = str(e)
    wizard_log(f"bundle: {bundle.version if bundle else bundle_error}")

    ws: dict[str, Any] = {
        "target": saved.get("target", "ryujinx"),
        "roots": {t: saved.get(f"{t}_root", "") for t in deploy.TARGETS},
        "result": None,
    }
    try:
        detected_host = deploy.detect_bridge_host()
    except Exception as e:  # noqa: BLE001
        wizard_log(f"LAN address detection failed: {e!r}")
        detected_host = ""
    if not ws["roots"]["ryujinx"]:
        ws["roots"]["ryujinx"] = str(deploy.detect_ryujinx() or "")
    if not ws["roots"]["sd"]:
        cards = deploy.detect_sd_cards()
        ws["roots"]["sd"] = str(cards[0]) if cards else ""

    # ---- widgets -----------------------------------------------------------

    def text(markup: str, height: float | None = None, **kw) -> Label:
        lab = Label(text=markup, markup=True, halign="left", valign="top",
                    size_hint_y=None, **kw)
        def refit(*_):
            lab.text_size = (lab.width, None)
            if height is None:
                lab.height = max(lab.texture_size[1], dp(24))
        lab.bind(width=refit, texture_size=refit)
        if height is not None:
            lab.height = dp(height)
        return lab

    def h1(title: str) -> Label:
        return text(f"[size=22sp][b]{title}[/b][/size]", height=40)

    def button(label: str, on_press, **kw) -> Button:
        b = Button(text=label, size_hint_y=None, height=dp(44), **kw)
        b.bind(on_release=lambda *_: on_press())
        return b

    def page(name: str) -> tuple[Screen, BoxLayout]:
        s = Screen(name=name)
        box = BoxLayout(orientation="vertical", padding=dp(20), spacing=dp(10))
        s.add_widget(box)
        return s, box

    sm = ScreenManager()

    # ---- 1. welcome --------------------------------------------------------

    s1, b1 = page("welcome")
    b1.add_widget(h1("Mario Kart 8 Deluxe Archipelago - Setup"))
    b1.add_widget(text(
        "This installs the Switch mod that connects the game to the MK8DX Client.\n\n"
        "[b]You need[/b]\n"
        f"  - Mario Kart 8 Deluxe with update [b]{GAME_VERSION}[/b] (the mod stays inactive on any "
        "other version)\n"
        "  - Ryujinx on this PC, or a Switch running Atmosphere on the same network as this PC\n\n"
        "[b]Good to know[/b]\n"
        "  - Nothing is compiled and no game files are read: the mod ships ready-made in the "
        "apworld; setup only copies it and records this PC's address for the Switch.\n"
        "  - Do not play online with the mod installed on a real Switch. 'Remove' on the next "
        "page takes it out again.\n"
        "  - Re-run setup ([b]/setup[/b] in the client) after updating the apworld."))
    b1.add_widget(Widget())
    if bundle:
        b1.add_widget(text(f"Bundled mod: [b]{bundle.version}[/b] (verified)", height=28))
    else:
        b1.add_widget(text(f"[color=#ff5252]Cannot install: {bundle_error}[/color]"))
    nav1 = BoxLayout(size_hint_y=None, height=dp(44), spacing=dp(8))
    nav1.add_widget(button("Close", lambda: App.get_running_app().stop(), size_hint_x=0.3))
    nxt = button("Next", lambda: setattr(sm, "current", "install"), size_hint_x=0.7)
    nxt.disabled = bundle is None
    nav1.add_widget(nxt)
    b1.add_widget(nav1)
    sm.add_widget(s1)

    # ---- 2. install --------------------------------------------------------

    s2, b2 = page("install")
    b2.add_widget(h1("Where does the game run?"))
    toggles = BoxLayout(size_hint_y=None, height=dp(44), spacing=dp(6))
    toggle_buttons: dict[str, ToggleButton] = {}
    for t in deploy.TARGETS:
        tb = ToggleButton(text=deploy.TARGET_LABELS[t].split(" (")[0], group="target",
                          allow_no_selection=False, state="down" if t == ws["target"] else "normal")
        tb.bind(on_release=lambda _b, t=t: select_target(t))
        toggle_buttons[t] = tb
        toggles.add_widget(tb)
    b2.add_widget(toggles)
    hint = text("", height=44)
    b2.add_widget(hint)
    path_row = BoxLayout(size_hint_y=None, height=dp(40), spacing=dp(6))
    path_input = TextInput(multiline=False, size_hint_x=0.75)
    path_row.add_widget(path_input)
    path_row.add_widget(button("Browse...", lambda: browse(), size_hint_x=0.25))
    b2.add_widget(path_row)
    drives_row = BoxLayout(size_hint_y=None, height=dp(40), spacing=dp(6))
    b2.add_widget(drives_row)
    host_row = BoxLayout(size_hint_y=None, height=dp(40), spacing=dp(6))
    host_row.add_widget(text("This PC's address:", height=40, size_hint_x=0.3))
    host_input = TextInput(multiline=False, size_hint_x=0.4,
                           text=saved.get("bridge_host") or detected_host)
    host_row.add_widget(host_input)
    host_row.add_widget(button("Detect", lambda: setattr(host_input, "text", deploy.detect_bridge_host()),
                               size_hint_x=0.3))
    b2.add_widget(host_row)
    b2.add_widget(text("[size=13sp]The Switch finds the MK8DX Client by searching this address's network. "
                       "Detected automatically; change it only if this PC has several network adapters "
                       "(VPN, WSL, virtual machines). Ryujinx on this PC does not need it.[/size]", height=40))
    status_label = text("", height=60)
    b2.add_widget(status_label)
    b2.add_widget(Widget())
    error_label = text("", height=60)
    b2.add_widget(error_label)
    nav2 = BoxLayout(size_hint_y=None, height=dp(44), spacing=dp(8))
    nav2.add_widget(button("Back", lambda: setattr(sm, "current", "welcome"), size_hint_x=0.25))
    remove_btn = button("Remove mod", lambda: do_remove(), size_hint_x=0.25)
    nav2.add_widget(remove_btn)
    install_btn = button("Install", lambda: do_install(), size_hint_x=0.5)
    nav2.add_widget(install_btn)
    b2.add_widget(nav2)
    sm.add_widget(s2)

    hints = {
        "ryujinx": "Ryujinx's data folder (contains 'mods' and 'Config.json'). For a portable "
                   "Ryujinx, pick the folder with Ryujinx.exe.",
        "sd": "The SD card's drive. Cards with an 'atmosphere' folder are listed below.",
        "folder": "Any folder. The Atmosphere layout is created inside it for you to copy to the SD card.",
    }

    def current_root() -> Path | None:
        raw = path_input.text.strip()
        return Path(raw) if raw else None

    def refresh_status(*_):
        t = ws["target"]
        ws["roots"][t] = path_input.text.strip()
        root = current_root()
        if root is None:
            status_label.text = "[i]Choose a location.[/i]"
            remove_btn.disabled = True
            return
        st = deploy.status(bundle, t, root)
        colour = {"current": "66bb6a", "different": "ffa726", "missing": "bdbdbd"}[st.state]
        status_label.text = (f"Installs to: {deploy.exefs_dir(t, root)}\n"
                             f"Now there: [color=#{colour}]{st.detail}[/color]")
        remove_btn.disabled = st.state == "missing"

    def select_target(t: str) -> None:
        if ws.get("shown"):  # the box is still empty on the first call
            ws["roots"][ws["target"]] = path_input.text.strip()
        ws["shown"] = True
        ws["target"] = t
        hint.text = hints[t]
        path_input.text = ws["roots"].get(t, "")
        drives_row.clear_widgets()
        if t == "sd":
            cards = deploy.detect_sd_cards()
            for c in cards:
                drives_row.add_widget(button(str(c), lambda c=c: setattr(path_input, "text", str(c))))
            if not cards:
                drives_row.add_widget(text("[i]No SD card with an 'atmosphere' folder found. Insert it, "
                                           "or browse to it.[/i]", height=40))
            drives_row.add_widget(button("Re-detect", lambda: select_target("sd"), size_hint_x=0.3))
        error_label.text = ""
        refresh_status()

    def browse() -> None:
        chosen = _ask_directory("Choose a location", path_input.text.strip() or None)
        if chosen:
            path_input.text = chosen

    def do_install() -> None:
        t = ws["target"]
        root = current_root()
        error_label.text = ""
        try:
            assert bundle is not None
            result = deploy.deploy(bundle, t, root, host_input.text.strip() or None)
        except deploy.DeployError as e:
            error_label.text = f"[color=#ff5252]{e}[/color]"
            wizard_log(f"deploy {t} {root} FAILED: {e}")
            return
        except Exception as e:  # noqa: BLE001
            error_label.text = f"[color=#ff5252]Unexpected error: {e!r} (details in {wizard_log_path()})[/color]"
            wizard_log(f"deploy {t} {root} CRASHED:\n{traceback.format_exc()}")
            return
        state.record_deploy(t, root, bundle.version, result.bridge_host)
        ws["result"] = result
        wizard_log(f"deploy {t}: {result.summary()}")
        show_done()

    def do_remove() -> None:
        t = ws["target"]
        try:
            lines = deploy.remove(t, current_root())
        except deploy.DeployError as e:
            error_label.text = f"[color=#ff5252]{e}[/color]"
            return
        st = state.load()
        if st.get("target") == t:
            st.pop("deployed_version", None)
            state.save(st)
        wizard_log(f"remove {t}: {lines}")
        error_label.text = "; ".join(lines)
        refresh_status()

    path_input.bind(text=refresh_status)

    # ---- 3. done -----------------------------------------------------------

    s3, b3 = page("done")
    b3.add_widget(h1("Installed"))
    done_body = text("")
    b3.add_widget(done_body)
    fw_box = BoxLayout(orientation="vertical", size_hint_y=None, height=dp(130), spacing=dp(6))
    fw_label = text("", height=74)
    fw_box.add_widget(fw_label)
    fw_btn = button("Allow through Windows Firewall (asks for admin)", lambda: do_firewall())
    fw_box.add_widget(fw_btn)
    b3.add_widget(fw_box)
    b3.add_widget(Widget())
    nav3 = BoxLayout(size_hint_y=None, height=dp(44), spacing=dp(8))
    nav3.add_widget(button("Back", lambda: setattr(sm, "current", "install"), size_hint_x=0.3))
    nav3.add_widget(button("Close", lambda: App.get_running_app().stop(), size_hint_x=0.7))
    b3.add_widget(nav3)
    sm.add_widget(s3)

    def refresh_firewall() -> None:
        missing = firewall.missing_rules()
        if not missing:
            fw_label.text = "[color=#66bb6a]Windows Firewall: TCP 17778 and UDP 17779 are allowed.[/color]"
            fw_btn.disabled = True
        else:
            fw_label.text = ("[b]Windows Firewall[/b]: the Switch reaches this PC on TCP 17778 and UDP "
                             "17779. If Windows did not ask you to allow the client, or you said no, "
                             "add the rules (they apply to Private networks: mark your Wi-Fi Private).")
            fw_btn.disabled = False

    def do_firewall() -> None:
        ok, msg = firewall.add_rules()
        wizard_log(f"firewall: {ok} {msg}")
        refresh_firewall()
        if not ok:
            fw_label.text = f"[color=#ffa726]{msg}[/color]"

    def show_done() -> None:
        t = ws["target"]
        result = ws["result"]
        done_body.text = (f"{result.summary()}\n\n[b]Next[/b]\n{NEXT_STEPS[t]}\n\n{CONNECT_TEXT}")
        fw_box.opacity = 1 if t != "ryujinx" else 0
        fw_box.disabled = t == "ryujinx"
        if t != "ryujinx":
            refresh_firewall()
        sm.current = "done"

    class SetupApp(App):
        title = "MK8DX Archipelago - Setup"

        def build(self_app):  # noqa: N805
            Window.size = (dp(780), dp(600))
            select_target(ws["target"])
            return sm

    try:
        SetupApp().run()
    except Exception:
        wizard_log(f"wizard crashed:\n{traceback.format_exc()}")
        raise
    wizard_log("=== wizard closed ===")


def main() -> None:
    """Subprocess entry (also `python -m ...wizard`)."""
    run_wizard()


if __name__ == "__main__":  # pragma: no cover
    main()
