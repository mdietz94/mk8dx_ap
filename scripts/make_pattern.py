#!/usr/bin/env python3
"""Derive a byte pattern for a symbol from a decompressed main NSO.

Given the main module's .text (a raw dump or a decompressed NSO whose text
segment starts at file offset 0) and a known main-relative offset, print a
TOML snippet with a pattern that matches exactly once, so a `offset_305`
fallback in switch-mod/syms/mk8dx_symbols.toml can be promoted to a
version-tolerant pattern.

    python scripts/make_pattern.py --text main.text.bin --offset 0x87CDB8 \
        --name "gear::RaceKartChecker::getGoalReactionByRank"

The pattern is taken from the function's first bytes with ADRP/ADR/BL/B
immediates wildcarded (they change between versions). Length grows until
the match is unique.
"""

from __future__ import annotations

import argparse
import sys
from pathlib import Path


def _wildcard_mask(instr: int) -> str:
    """Return a 4-byte pattern fragment for one AArch64 instruction with
    relocatable immediates wildcarded (little-endian byte order)."""
    b = instr.to_bytes(4, "little")
    top = (instr >> 24) & 0xFF
    # BL / B: 0x94..0x97 / 0x14..0x17 -> only the opcode byte is stable
    if 0x94 <= top <= 0x97 or 0x14 <= top <= 0x17:
        return f"?? ?? ?? {b[3]:02X}"
    # ADRP / ADR: bits 31 (op), 28..24 = 10000; immediates elsewhere
    if (instr & 0x1F000000) == 0x10000000:
        return f"?? ?? ?? {b[3] & 0x9F:02X}"
    # LDR literal / conditional branches: wildcard the middle bytes
    if (instr & 0xFF000010) == 0x54000000 or (instr & 0xBF000000) == 0x18000000:
        return f"{b[0]:02X} ?? ?? {b[3]:02X}"
    return " ".join(f"{x:02X}" for x in b)


def pattern_at(text: bytes, offset: int, n_instrs: int) -> str:
    parts = []
    for i in range(n_instrs):
        o = offset + i * 4
        instr = int.from_bytes(text[o:o + 4], "little")
        parts.append(_wildcard_mask(instr))
    return " ".join(parts)


def count_matches(text: bytes, pattern: str) -> int:
    toks = pattern.split()
    first_fixed = next((i for i, t in enumerate(toks) if t != "??"), None)
    if first_fixed is None:
        return 0
    anchor = int(toks[first_fixed], 16)
    n = 0
    L = len(toks)
    for i in range(len(text) - L + 1):
        if text[i + first_fixed] != anchor:
            continue
        ok = True
        for j, t in enumerate(toks):
            if t != "??" and text[i + j] != int(t, 16):
                ok = False
                break
        if ok:
            n += 1
            if n > 1:
                return n
    return n


def main(argv: list[str] | None = None) -> int:
    p = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    p.add_argument("--text", required=True, help="raw .text dump (decompressed main NSO text segment)")
    p.add_argument("--offset", required=True, help="main-relative offset, e.g. 0x87CDB8")
    p.add_argument("--name", required=True)
    p.add_argument("--mangled", default="")
    p.add_argument("--max-instrs", type=int, default=24)
    a = p.parse_args(argv)
    text = Path(a.text).read_bytes()
    off = int(a.offset, 0)
    for n in range(4, a.max_instrs + 1):
        pat = pattern_at(text, off, n)
        if count_matches(text, pat) == 1:
            print("[[symbols]]")
            print(f'name = "{a.name}"')
            print(f'mangled = "{a.mangled}"')
            print(f'pattern = "{pat}"')
            print('offset = "0x0"')
            print(f'offset_305 = "{a.offset}"')
            return 0
    print("no unique pattern within max-instrs; widen --max-instrs", file=sys.stderr)
    return 1


if __name__ == "__main__":
    sys.exit(main())
