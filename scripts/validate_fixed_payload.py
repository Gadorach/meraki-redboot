#!/usr/bin/env python3
"""Validate a fixed-address freestanding MIPS executable before flattening it."""
from __future__ import annotations

import argparse
import re
import subprocess
import sys
from pathlib import Path

FORBIDDEN = {
    ".dynamic", ".dynsym", ".plt", ".got", ".got.plt",
    ".rel.dyn", ".rela.dyn", ".interp",
}


def parse_int(value: str) -> int:
    return int(value, 0)


def run(tool: str, *args: str) -> str:
    result = subprocess.run([tool, *args], text=True, capture_output=True, check=False)
    if result.returncode:
        raise RuntimeError(f"{tool} {' '.join(args)} failed:\n{result.stderr}")
    return result.stdout


def allocated_extent(readelf: str, elf: Path) -> tuple[int, int, set[str], list[str]]:
    output = run(readelf, "-SW", str(elf))
    names: set[str] = set()
    allocated: list[tuple[str, int, int, str, str]] = []
    lines: list[str] = []
    pattern = re.compile(
        r"^\s*\[\s*\d+\]\s+(\S+)\s+(\S+)\s+([0-9a-fA-F]+)\s+"
        r"[0-9a-fA-F]+\s+([0-9a-fA-F]+)\s+[0-9a-fA-F]+\s+(\S*)"
    )
    for line in output.splitlines():
        match = pattern.match(line)
        if not match:
            continue
        name, section_type, address_text, size_text, flags = match.groups()
        names.add(name)
        address = int(address_text, 16)
        size = int(size_text, 16)
        if size and "A" in flags:
            allocated.append((name, address, size, flags, section_type))
            lines.append(
                f"section={name} address=0x{address:08x} size=0x{size:x} "
                f"type={section_type} flags={flags}"
            )
    if not allocated:
        raise ValueError("ELF has no allocated sections")
    start = min(address for _name, address, _size, _flags, _type in allocated)
    end = max(address + size for _name, address, size, _flags, _type in allocated)
    return start, end, names, lines


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--elf", type=Path, required=True)
    parser.add_argument("--entry", type=parse_int, required=True)
    parser.add_argument("--entry-symbol", default="_start")
    parser.add_argument("--maximum-size", type=parse_int)
    parser.add_argument("--range-end", type=parse_int)
    parser.add_argument("--readelf", required=True)
    parser.add_argument("--nm", required=True)
    parser.add_argument("--report", type=Path)
    args = parser.parse_args()
    report: list[str] = [f"elf={args.elf}", f"entry_symbol={args.entry_symbol}"]
    try:
        symbols = run(args.nm, "-n", str(args.elf))
        entries = []
        undefined = []
        for line in symbols.splitlines():
            fields = line.split()
            if len(fields) >= 3 and fields[-1] == args.entry_symbol:
                entries.append(int(fields[0], 16) & 0xffffffff)
            if len(fields) >= 2 and fields[-2].upper() == "U":
                undefined.append(fields[-1])
        if entries != [args.entry]:
            raise ValueError(
                f"{args.entry_symbol} must be 0x{args.entry:08x}; found {entries!r}"
            )
        if undefined:
            raise ValueError("unresolved symbols: " + ", ".join(sorted(set(undefined))))

        relocations = run(args.readelf, "-rW", str(args.elf))
        if re.search(r"\bR_MIPS_", relocations):
            raise ValueError("final fixed-address ELF still contains unresolved MIPS relocations")

        start, end, names, section_lines = allocated_extent(args.readelf, args.elf)
        report.extend(section_lines)
        if start != args.entry:
            raise ValueError(
                f"first allocated byte is 0x{start:08x}, expected entry 0x{args.entry:08x}"
            )
        image_size = end - start
        if args.maximum_size is not None and image_size > args.maximum_size:
            raise ValueError(
                f"allocated image size 0x{image_size:x} exceeds maximum 0x{args.maximum_size:x}"
            )
        if args.range_end is not None and end > args.range_end:
            raise ValueError(
                f"allocated image ends at 0x{end:08x}, beyond reserved range end "
                f"0x{args.range_end:08x}"
            )
        bad = sorted(names & FORBIDDEN)
        if bad:
            raise ValueError(f"forbidden runtime-linker sections: {', '.join(bad)}")
        if ".bss" in names:
            sections = run(args.readelf, "-SW", str(args.elf))
            match = re.search(r"^\s*\[\s*\d+\]\s+\.bss\s+\S+\s+[0-9a-fA-F]+\s+"
                              r"[0-9a-fA-F]+\s+([0-9a-fA-F]+)", sections, re.M)
            if match and int(match.group(1), 16):
                raise ValueError("fixed-address image has a non-empty BSS")
        report.extend([
            f"entry=0x{args.entry:08x}",
            f"allocated_start=0x{start:08x}",
            f"allocated_end=0x{end:08x}",
            f"allocated_size=0x{image_size:x}",
            "runtime_relocations=none",
            "undefined_symbols=none",
            "result=pass",
        ])
    except (OSError, RuntimeError, ValueError) as exc:
        report.extend([f"result=fail", f"error={exc}"])
        if args.report:
            args.report.parent.mkdir(parents=True, exist_ok=True)
            args.report.write_text("\n".join(report) + "\n")
        print(f"fixed-payload validation failed: {exc}", file=sys.stderr)
        return 1

    if args.report:
        args.report.parent.mkdir(parents=True, exist_ok=True)
        args.report.write_text("\n".join(report) + "\n")
    print(
        f"fixed payload {args.entry_symbol} verified at 0x{args.entry:08x}; "
        f"size=0x{image_size:x}; no runtime relocations"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
