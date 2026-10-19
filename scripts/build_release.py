"""Build the release mk8dx.apworld: network-agnostic mod + apworld + audit + checksum.

    python scripts/build_release.py                 # -> dist/mk8dx.apworld + .sha256
    python scripts/build_release.py --skip-build    # reuse switch-mod/build/exefs (must be a release build)
    python scripts/build_release.py --publish       # + gh release create v<version> (tag must exist at HEAD)

The version is archipelago.json `world_version`; client/__init__.py
`__version__` must match (the audit fails on drift), and the mod is built
with -DMK8AP_MOD_VERSION=<version> so the client can tell when the Switch
runs an older mod. Nothing network-specific is compiled in (the discovery
seed is the ap_config.json the wizard writes); the audit rejects a mod with
a baked IPv4 address. The GitHub release workflow runs this script.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import subprocess
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
DIST = REPO / "dist"
APWORLD = DIST / "mk8dx.apworld"


def run(cmd: list[str]) -> None:
    print("+ " + " ".join(cmd), flush=True)
    rc = subprocess.run(cmd, cwd=REPO).returncode
    if rc != 0:
        sys.exit(f"FAIL: {cmd[1] if len(cmd) > 1 else cmd[0]} exited {rc}")


def version() -> str:
    return json.loads((REPO / "apworld/mk8dx_archipelago/archipelago.json").read_text(encoding="utf-8"))["world_version"]


def main(argv: list[str] | None = None) -> int:
    p = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    p.add_argument("--skip-build", action="store_true", help="bundle the existing switch-mod/build/exefs")
    p.add_argument("--publish", action="store_true", help="create the GitHub release with gh")
    args = p.parse_args(argv)

    ver = version()
    py = sys.executable
    if not args.skip_build:
        run([py, "scripts/build_switchmod.py", f"-DMK8AP_MOD_VERSION={ver}"])
    DIST.mkdir(exist_ok=True)
    run([py, "scripts/install_apworld.py", "--bundle-mod", "--mod-version", ver, "--output", str(APWORLD)])
    run([py, "scripts/release_audit.py", str(APWORLD)])
    digest = hashlib.sha256(APWORLD.read_bytes()).hexdigest()
    sha = APWORLD.with_name(APWORLD.name + ".sha256")
    sha.write_text(f"{digest}  {APWORLD.name}\n", encoding="ascii")
    print(f"OK: {APWORLD} ({APWORLD.stat().st_size / 1024:.1f} KiB) v{ver}\n    sha256 {digest}")

    if args.publish:
        tag = f"v{ver}"
        head = subprocess.run(["git", "rev-parse", "HEAD"], cwd=REPO, capture_output=True, text=True).stdout.strip()
        tagged = subprocess.run(["git", "rev-parse", f"{tag}^{{commit}}"], cwd=REPO,
                                capture_output=True, text=True).stdout.strip()
        if tagged != head:
            sys.exit(f"FAIL: tag {tag} must exist and point at HEAD ({head[:10]}); "
                     f"create it with: git tag -a {tag} -m {tag}")
        cmd = ["gh", "release", "create", tag, str(APWORLD), str(sha), "--title", tag,
               "--generate-notes", "--notes-file", str(REPO / "docs" / "release-notes-template.md")]
        if "-" in ver:
            cmd.append("--prerelease")
        run(cmd)
    return 0


if __name__ == "__main__":
    sys.exit(main())
