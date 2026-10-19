"""Kivy UI for the MK8DX Client. Only imported from MK8DXContext.run_gui."""

from __future__ import annotations

import logging
import time
import typing

from kvui import GameManager, UILog  # must precede any kivy.* import

from kivy.clock import Clock
from kivy.metrics import dp
from kivy.uix.boxlayout import BoxLayout
from kivy.uix.label import Label
from kivy.uix.scrollview import ScrollView

from . import tables

if typing.TYPE_CHECKING:  # pragma: no cover
    from .context import MK8DXContext

_REFRESH_INTERVAL = 1.5


class _LiveLabel(Label):
    def __init__(self, **kwargs):
        super().__init__(markup=True, valign="top", halign="left", size_hint_y=None,
                         padding=(dp(10), dp(10)), **kwargs)
        self.bind(width=self._refit, texture_size=self._refit)

    def _refit(self, *_):
        self.text_size = (self.width - dp(20), None)
        self.height = max(self.texture_size[1] + dp(20), dp(60))


class MK8DXManager(GameManager):
    logging_pairs = [("Client", "Archipelago")]
    base_title = "Archipelago MK8DX Client"

    def __init__(self, ctx: "MK8DXContext"):
        super().__init__(ctx)
        self._label: _LiveLabel | None = None

    def build(self):
        container = super().build()
        split = BoxLayout(orientation="horizontal", spacing=dp(4))
        left = ScrollView(do_scroll_x=False, do_scroll_y=True, size_hint_x=0.5)
        self._label = _LiveLabel(text="(starting)")
        left.add_widget(self._label)
        split.add_widget(left)
        mk8_log = UILog(logging.getLogger("MK8DX"))
        mk8_log.size_hint_x = 0.5
        split.add_widget(mk8_log)
        self.add_client_tab("Kart", split)
        Clock.schedule_interval(self._refresh, _REFRESH_INTERVAL)
        return container

    def _refresh(self, _dt) -> None:
        try:
            if self._label is not None:
                self._label.text = _format_status(self.ctx)
        except Exception:  # noqa: BLE001
            logging.getLogger("MK8DX").exception("panel refresh failed")


def _mod_line(ctx: "MK8DXContext") -> str:
    try:
        from .._setup import state as setup_state
        st = setup_state.load()
    except Exception:  # noqa: BLE001
        st = {}
    bundled = ctx.switch.bundled_mod_ver if ctx.switch is not None else None
    deployed = st.get("deployed_version")
    if not deployed:
        return "[color=#ff9800]not installed[/color] - type /setup" if bundled else "(source build)"
    txt = f"{deployed} on {st.get('target', '?')}"
    if bundled and deployed != bundled:
        txt += f"  [color=#ff9800]bundled {bundled}: type /setup[/color]"
    return txt


def _format_status(ctx: "MK8DXContext") -> str:
    st = ctx.state
    s = st.summary()
    parts: list[str] = []
    sw = s["switch_info"]
    sw_txt = "connected" if s["switch"] == "connected" else "[color=#ff9800]not connected[/color]"
    if sw.get("mod_ver"):
        sw_txt += f"  mod {sw.get('mod_ver')} / game {sw.get('game_ver')} / symbols {sw.get('resolved')}/{sw.get('total')}"
    parts.append(f"[b]Switch[/b]: {sw_txt}")
    parts.append(f"[b]Mod[/b]: {_mod_line(ctx)}")
    parts.append(f"[b]Top speed[/b]: {s['speed_pct']}%  ({st.count(tables.PROGRESSIVE_SPEED)}/{st.speed_stages} stages)")
    cups = [c.name for c in tables.CUPS if st.has(tables.cup_item(c)) or c.name == tables.STARTING_CUP]
    parts.append(f"[b]Cups[/b] ({len(cups)}): " + (", ".join(cups) if cups else "[i]none[/i]"))
    if st.shuffle_characters:
        drivers = [e.name for e in tables.DRIVERS if st.has(tables.driver_item(e)) or e.name == tables.STARTING_DRIVER]
        parts.append(f"[b]Characters[/b] ({len(drivers)}): " + ", ".join(drivers))
    if st.shuffle_items:
        items = [e.name for e in tables.ITEMS if st.has(tables.item_item(e)) or e.name in tables.STARTING_ITEMS]
        parts.append(f"[b]Item box[/b] ({len(items)}): " + ", ".join(items))
    if st.shuffle_vehicles:
        n = sum(1 for e in tables.BODIES if st.has(tables.body_item(e))) + \
            sum(1 for e in tables.TIRES if st.has(tables.tire_item(e))) + \
            sum(1 for e in tables.GLIDERS if st.has(tables.glider_item(e)))
        parts.append(f"[b]Vehicle parts[/b] unlocked: {n} (+ Standard set)")
    parts.append(f"[b]Learned ids[/b]: {s['learned']}")
    parts.append("")
    parts.append("[b]Recent races[/b]")
    for ev in st.recent_races[-8:][::-1]:
        parts.append(f"  {time.strftime('%H:%M:%S', time.localtime(ev.ts))} {ev.course} - rank {ev.rank} "
                     f"-> {', '.join(ev.checked) if ev.checked else '[i]no new check[/i]'}")
    if not st.recent_races:
        parts.append("  [i](none yet)[/i]")
    return "\n".join(parts)
