"""Windows Firewall rules for the client's two inbound ports.

A real Switch reaches the PC on UDP 17779 (discovery probe) and TCP 17778
(the bridge). Windows asks once, the first time the client listens; if the
player dismissed that prompt, or the network is marked Public, both are
dropped silently and the Switch never finds the PC. Ryujinx on the same PC
uses loopback and needs neither rule.

Adding a rule needs admin rights, so `add_rules()` runs one elevated
`netsh` batch through ShellExecute "runas" (one UAC prompt).
"""

from __future__ import annotations

import subprocess
import sys

RULES = (
    ("MK8DX Archipelago bridge (TCP 17778)", "TCP", 17778),
    ("MK8DX Archipelago discovery (UDP 17779)", "UDP", 17779),
)
_NO_WINDOW = getattr(subprocess, "CREATE_NO_WINDOW", 0)


def add_rule_args(name: str, protocol: str, port: int) -> list[str]:
    return ["advfirewall", "firewall", "add", "rule", f"name={name}", "dir=in",
            "action=allow", f"protocol={protocol}", f"localport={port}", "profile=private,domain"]


def manual_commands() -> list[str]:
    """What to paste into an elevated prompt if the UAC route fails."""
    out = []
    for name, proto, port in RULES:
        args = add_rule_args(name, proto, port)
        args[4] = f'name="{name}"'
        out.append("netsh " + " ".join(args))
    return out


def rule_present(name: str) -> bool:
    if sys.platform != "win32":
        return True
    try:
        r = subprocess.run(["netsh", "advfirewall", "firewall", "show", "rule", f"name={name}"],
                           capture_output=True, text=True, timeout=10, creationflags=_NO_WINDOW)
    except (OSError, subprocess.TimeoutExpired):
        return False
    return r.returncode == 0 and name in r.stdout


def missing_rules() -> list[str]:
    return [name for name, _, _ in RULES if not rule_present(name)]


def add_rules() -> tuple[bool, str]:
    """Add the missing rules via one elevated cmd. Returns (ok, message)."""
    if sys.platform != "win32":
        return True, "not Windows: nothing to do"
    missing = missing_rules()
    if not missing:
        return True, "firewall rules already present"
    cmds = []
    for name, proto, port in RULES:
        if name in missing:
            args = add_rule_args(name, proto, port)
            args[4] = f'name="{name}"'
            cmds.append("netsh " + " ".join(args))
    try:
        import ctypes
        rc = ctypes.windll.shell32.ShellExecuteW(None, "runas", "cmd.exe",
                                                 "/c " + " & ".join(cmds), None, 0)
    except Exception as e:  # noqa: BLE001
        return False, f"could not request admin rights ({e}); run in an elevated prompt:\n" + \
            "\n".join(manual_commands())
    if rc <= 32:  # ShellExecute error codes; 5 = the UAC prompt was declined
        return False, "admin rights were declined; run in an elevated prompt:\n" + \
            "\n".join(manual_commands())
    # The elevated cmd runs asynchronously; give it a moment, then re-check.
    import time
    for _ in range(20):
        time.sleep(0.25)
        if not missing_rules():
            return True, "firewall rules added"
    return False, "the rules did not appear; run in an elevated prompt:\n" + "\n".join(manual_commands())
