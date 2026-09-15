"""MK8DX Client entry point (Archipelago Launcher "MK8DX Client" button).

    python vendor/Archipelago/Launcher.py "MK8DX Client" --connect localhost:38281 --name Mario

Three things share the asyncio loop: the AP websocket (CommonClient), the
Switch TCP server (port 17778) + UDP discovery responder (port 17779), and
the Kivy UI.
"""

from __future__ import annotations

import argparse
import asyncio
import logging
import sys
from pathlib import Path

import Utils
from CommonClient import gui_enabled, get_base_parser, server_loop

from . import __version__, logging_setup
from .context import MK8DXContext
from .discovery import DEFAULT_DISCOVERY_PORT, DiscoveryResponder
from .learned import LearnedIds
from .state import BridgeState
from .switch_server import SwitchServer

log = logging.getLogger("MK8DX")

DEFAULT_SWITCH_PORT = 17778


def _load_settings():
    try:
        from .. import MK8DXWorld  # type: ignore[attr-defined]
        return MK8DXWorld.settings
    except Exception:
        class _Defaults:
            switch_listen_host = "0.0.0.0"
            switch_listen_port = DEFAULT_SWITCH_PORT
        return _Defaults()


def parse_args(argv: list[str] | None = None) -> argparse.Namespace:
    p = get_base_parser(description="Mario Kart 8 Deluxe Archipelago Client")
    p.prog = "MK8DXClient"
    p.add_argument("--name", default=None, help="AP slot name")
    p.add_argument("--switch-host", default=None, help="Bind address for the Switch TCP server")
    p.add_argument("--switch-port", type=int, default=None, help="Port for the Switch TCP server (default 17778)")
    p.add_argument("--learned-ids", default=None, help="Path of the learned id-table JSON")
    p.add_argument("--log-level", default=None)
    p.add_argument("--version", action="version", version=__version__)
    return p.parse_args(argv)


def _learned_path(override: str | None) -> Path:
    if override:
        return Path(override)
    try:
        return Path(Utils.user_path("mk8dx_learned_ids.json"))
    except Exception:
        return Path.home() / ".archipelago" / "mk8dx_learned_ids.json"


async def main(args: argparse.Namespace) -> None:
    settings = _load_settings()
    host = args.switch_host or str(settings.switch_listen_host)
    port = args.switch_port or int(settings.switch_listen_port)
    logging_setup.setup(args.log_level or "INFO")
    log.info("MK8DX Client %s starting; Switch listen %s:%d", __version__, host, port)

    state = BridgeState(LearnedIds(_learned_path(args.learned_ids)))
    state.slot = args.name or ""

    server_addr = args.connect if args.connect else None
    ctx = MK8DXContext(server_addr, args.password or None, state)
    ctx.auth = args.name or None

    sw = SwitchServer(host, port, state, client_ver=__version__,
                      on_race_result=ctx.on_race_result,
                      on_play_report=ctx.on_play_report,
                      build_state=state.build_state)
    ctx.switch = sw
    await sw.start()

    discovery = DiscoveryResponder(tcp_port=port, get_seed=lambda: state.seed,
                                   port=DEFAULT_DISCOVERY_PORT)
    await discovery.start()

    if args.connect:
        asyncio.create_task(ctx.connect(), name="initial-connect")

    if gui_enabled:
        ctx.run_gui()
    ctx.run_cli()

    try:
        await ctx.exit_event.wait()
    except (KeyboardInterrupt, asyncio.CancelledError):
        pass
    finally:
        await sw.stop()
        discovery.stop()
        state.learned.save()
        await ctx.shutdown()


def launch(*launch_args: str) -> None:
    args = parse_args(list(launch_args))
    Utils.init_logging("MK8DXClient", exception_logger="Client")
    try:
        asyncio.run(main(args))
    except KeyboardInterrupt:
        pass


if __name__ == "__main__":  # pragma: no cover
    launch(*sys.argv[1:])
