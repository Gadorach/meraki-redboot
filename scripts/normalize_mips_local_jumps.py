#!/usr/bin/env python3
"""Rewrite GCC-generated local MIPS `j` instructions as PC-relative `b`.

The reset-time loader is linked at offset zero but executes from active and
fallback flash copies.  MIPS J-format local jumps retain only the upper nibble
of the runtime PC and therefore lose the loader-copy base.  Every generated
local target is within the signed 16-bit branch range, so a PC-relative branch
preserves identical control flow at every loader location.

Only compiler-local labels (`$L...`, `$BB...`, or `.L...`) are rewritten.  Returns (`j
$31`) and all other targets are left untouched and are subsequently audited by
the object-code validator.
"""

from __future__ import annotations

import argparse
import re
from pathlib import Path

LOCAL_JUMP_RE = re.compile(
    r"^(?P<indent>\s*)j(?P<space>\s+)(?P<label>(?:\$L|\$BB|\.L)[A-Za-z0-9_.$]*)(?P<tail>\s*(?:#.*)?)$"
)


def normalize(source: str) -> tuple[str, int]:
    output: list[str] = []
    count = 0
    for line in source.splitlines(keepends=True):
        newline = "\n" if line.endswith("\n") else ""
        body = line[:-1] if newline else line
        match = LOCAL_JUMP_RE.match(body)
        if match:
            body = (
                f"{match.group('indent')}b{match.group('space')}"
                f"{match.group('label')}{match.group('tail')}"
            )
            count += 1
        output.append(body + newline)
    return "".join(output), count


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--input", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--count-file", type=Path)
    args = parser.parse_args()

    normalized, count = normalize(args.input.read_text())
    args.output.write_text(normalized)
    if args.count_file:
        args.count_file.write_text(f"{count}\n")
    print(f"normalized {count} local MIPS jump(s): {args.input} -> {args.output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
