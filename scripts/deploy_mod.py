"""Deploy a local mod build with the same code the setup wizard uses.

    python scripts/deploy_mod.py ryujinx                 # switch-mod/build/exefs -> Ryujinx
    python scripts/deploy_mod.py sd --root E:/
    python scripts/deploy_mod.py status
    python scripts/deploy_mod.py remove ryujinx

`deploy` defaults to --from-dir switch-mod/build/exefs; everything else is
passed to apworld/mk8dx_archipelago/_setup/cli.py unchanged.
"""

from __future__ import annotations

import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(REPO / "apworld" / "mk8dx_archipelago"))

from _setup.cli import main  # type: ignore[import-not-found]  # noqa: E402

if __name__ == "__main__":
    argv = sys.argv[1:]
    if argv and argv[0] in ("ryujinx", "sd", "folder"):
        argv = ["deploy", *argv]
    if argv and argv[0] == "deploy" and "--from-dir" not in argv:
        argv += ["--from-dir", str(REPO / "switch-mod" / "build" / "exefs")]
    sys.exit(main(argv))
