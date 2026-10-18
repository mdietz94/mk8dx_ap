"""Headless setup: the same steps as the wizard, for tests, the dev loop and
players who prefer a terminal.

    python -m worlds.mk8dx._setup.cli status
    python -m worlds.mk8dx._setup.cli deploy ryujinx [--root DIR]
    python -m worlds.mk8dx._setup.cli deploy sd --root E:/
    python -m worlds.mk8dx._setup.cli deploy folder --root C:/staging
    python -m worlds.mk8dx._setup.cli deploy ryujinx --from-dir switch-mod/build/exefs
    python -m worlds.mk8dx._setup.cli remove ryujinx
    python -m worlds.mk8dx._setup.cli firewall

From a source checkout `scripts/deploy_mod.py` forwards here. Exit status 0 on
success, 1 on a deploy error, 2 on bad arguments.
"""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

from . import deploy, firewall, state
from .bundle import BundleError, ModBundle, has_bundled_mod, load_bundled, load_from_dir


def default_root(target: str) -> Path | None:
    saved = state.load().get(f"{target}_root")
    if saved:
        return Path(saved)
    if target == "ryujinx":
        return deploy.detect_ryujinx()
    if target == "sd":
        cards = deploy.detect_sd_cards()
        return cards[0] if len(cards) == 1 else None
    return None


def _bundle(from_dir: str | None) -> ModBundle:
    return load_from_dir(Path(from_dir)) if from_dir else load_bundled()


def cmd_status(_args) -> int:
    bundle = None
    if has_bundled_mod():
        try:
            bundle = load_bundled()
            print(f"bundled mod: {bundle.version} (verified)")
        except BundleError as e:
            print(f"bundled mod: BROKEN: {e}")
    else:
        print("bundled mod: none in this apworld")
    st = state.load()
    print(f"last deploy: {st.get('deployed_version', 'never')} -> {st.get('target', '-')} "
          f"{st.get('deployed_at', '')}".rstrip())
    for target in deploy.TARGETS:
        root = default_root(target)
        if root is None:
            continue
        s = deploy.status(bundle, target, root)
        print(f"{target:8s} {s.exefs}: {s.state} ({s.detail})")
    missing = firewall.missing_rules()
    print("firewall: " + ("ok" if not missing else "missing " + ", ".join(missing)))
    return 0


def cmd_deploy(args) -> int:
    root = Path(args.root) if args.root else default_root(args.target)
    try:
        bundle = _bundle(args.from_dir)
        result = deploy.deploy(bundle, args.target, root)
    except (BundleError, deploy.DeployError) as e:
        print(f"FAIL: {e}", file=sys.stderr)
        return 1
    state.record_deploy(args.target, root, bundle.version)
    print(f"OK: mod {bundle.version}: {result.summary()}")
    return 0


def cmd_remove(args) -> int:
    root = Path(args.root) if args.root else default_root(args.target)
    try:
        for line in deploy.remove(args.target, root):
            print(line)
    except deploy.DeployError as e:
        print(f"FAIL: {e}", file=sys.stderr)
        return 1
    st = state.load()
    if st.get("target") == args.target:
        st.pop("deployed_version", None)
        state.save(st)
    return 0


def cmd_firewall(_args) -> int:
    ok, msg = firewall.add_rules()
    print(("OK: " if ok else "FAIL: ") + msg)
    return 0 if ok else 1


def build_parser() -> argparse.ArgumentParser:
    p = argparse.ArgumentParser(prog="mk8dx-setup", description="MK8DX Archipelago Switch mod setup")
    sub = p.add_subparsers(dest="cmd", required=True)
    sub.add_parser("status", help="show the bundled mod and what is deployed where").set_defaults(fn=cmd_status)
    d = sub.add_parser("deploy", help="copy the mod to Ryujinx, an SD card or a folder")
    d.add_argument("target", choices=deploy.TARGETS)
    d.add_argument("--root", help="Ryujinx data dir / SD root / folder (default: detected or last used)")
    d.add_argument("--from-dir", help="deploy a local build (e.g. switch-mod/build/exefs) instead of the bundled mod")
    d.set_defaults(fn=cmd_deploy)
    r = sub.add_parser("remove", help="remove the mod (restores whatever it replaced)")
    r.add_argument("target", choices=deploy.TARGETS)
    r.add_argument("--root")
    r.set_defaults(fn=cmd_remove)
    sub.add_parser("firewall", help="allow TCP 17778 + UDP 17779 in Windows Firewall (UAC prompt)") \
        .set_defaults(fn=cmd_firewall)
    return p


def main(argv: list[str] | None = None) -> int:
    args = build_parser().parse_args(argv)
    return args.fn(args)


if __name__ == "__main__":  # pragma: no cover
    sys.exit(main())
