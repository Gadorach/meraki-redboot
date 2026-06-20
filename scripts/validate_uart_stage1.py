#!/usr/bin/env python3
"""Validate the fixed-address UART RAM-loader stage-1 executable."""
from __future__ import annotations

import argparse
import re
import subprocess
import sys
from pathlib import Path

FORBIDDEN_SECTIONS = {
    ".interp", ".dynamic", ".dynsym", ".dynstr", ".rel.dyn", ".rela.dyn",
    ".plt", ".got", ".got.plt",
}


def integer(value: str) -> int:
    return int(value, 0)


def run(tool: str, *args: str) -> str:
    result = subprocess.run([tool, *args], text=True, capture_output=True, check=False)
    if result.returncode:
        raise RuntimeError(f"{tool} {' '.join(args)} failed:\n{result.stderr}")
    return result.stdout


def entry_address(readelf: str, elf: Path) -> int:
    text = run(readelf, "-hW", str(elf))
    match = re.search(r"Entry point address:\s*(0x[0-9a-fA-F]+)", text)
    if not match:
        raise ValueError("could not read ELF entry address")
    return int(match.group(1), 16)


def sections(readelf: str, elf: Path) -> dict[str, tuple[int, int]]:
    text = run(readelf, "-SW", str(elf))
    result: dict[str, tuple[int, int]] = {}
    # [ 1] .uart_stage1 PROGBITS a7f00000 010000 0039c0 ...
    pattern = re.compile(
        r"^\s*\[\s*\d+\]\s+(\S+)\s+\S+\s+([0-9a-fA-F]+)\s+"
        r"[0-9a-fA-F]+\s+([0-9a-fA-F]+)\b"
    )
    for line in text.splitlines():
        match = pattern.match(line)
        if match:
            result[match.group(1)] = (int(match.group(2), 16), int(match.group(3), 16))
    return result


def validate_calls(objdump: str, elf: Path, start: int, end: int) -> None:
    text = run(objdump, "-drwC", str(elf))
    offenders: list[str] = []
    for line in text.splitlines():
        if not re.match(r"^\s*[0-9a-fA-F]+:\s", line):
            continue
        # GNU objdump normally prints:  a7f00020: 0c... jal a7f00100 <...>
        match = re.search(r"\b(?:j|jal)\s+(?:0x)?([0-9a-fA-F]+)\b", line)
        if not match:
            continue
        instruction_address = int(line.split(":", 1)[0].strip(), 16)
        target = int(match.group(1), 16)
        # Some MIPS objdump versions print only the encoded low 28 bits for
        # J/JAL. At runtime the CPU supplies the upper nibble from PC+4.
        runtime_target = ((instruction_address + 4) & 0xF0000000) | (target & 0x0FFFFFFF)
        if not (start <= runtime_target < end):
            offenders.append(line.strip())
    if offenders:
        raise ValueError(
            "fixed-RAM stage contains direct jump/call targets outside its linked window:\n"
            + "\n".join(offenders[:24])
        )


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--elf", type=Path, required=True)
    ap.add_argument("--objdump", required=True)
    ap.add_argument("--readelf", required=True)
    ap.add_argument("--nm", required=True)
    ap.add_argument("--load-address", type=integer, required=True)
    ap.add_argument("--max-size", type=integer, required=True)
    args = ap.parse_args()

    try:
        if entry_address(args.readelf, args.elf) != args.load_address:
            raise ValueError(
                f"entry is not fixed at 0x{args.load_address:08x}"
            )
        sec = sections(args.readelf, args.elf)
        forbidden = sorted(set(sec) & FORBIDDEN_SECTIONS)
        if forbidden:
            raise ValueError(f"forbidden dynamic/GOT sections: {', '.join(forbidden)}")
        if ".uart_stage1" not in sec:
            raise ValueError("missing .uart_stage1 output section")
        address, size = sec[".uart_stage1"]
        if address != args.load_address:
            raise ValueError(
                f".uart_stage1 begins at 0x{address:08x}, expected 0x{args.load_address:08x}"
            )
        if size <= 0 or size > args.max_size:
            raise ValueError(
                f"stage size 0x{size:x} does not fit reserved 0x{args.max_size:x}-byte window"
            )
        unresolved = run(args.nm, "-u", str(args.elf)).strip()
        if unresolved:
            raise ValueError(f"unresolved symbols remain:\n{unresolved}")
        reloc_text = run(args.readelf, "-rW", str(args.elf))
        relocs = sorted(set(re.findall(r"\bR_MIPS_[A-Z0-9_]+\b", reloc_text)))
        if relocs:
            raise ValueError(f"final stage retains relocations: {', '.join(relocs)}")
        binary = args.elf.read_bytes()
        for marker in (b"PMOSRAM READY 2", b"PMOSBOOT FLASH HEADER=", b"PMOSBOOT EXEC "):
            if marker not in binary:
                raise ValueError(f"stage lacks required marker {marker!r}")
        symbols = run(args.nm, "-n", str(args.elf))
        symbol_names = {line.split()[-1] for line in symbols.splitlines() if line.split()}
        for required in ("uart_stage1_entry", "uart_stage1_main", "uart_stage1_jump_kernel", "uart_stage1_jump_fallback"):
            if required not in symbol_names:
                raise ValueError(f"stage lacks required symbol {required}")
        validate_calls(args.objdump, args.elf, address, address + size)
    except (OSError, RuntimeError, ValueError) as exc:
        print(f"UART-stage1 validation failed: {exc}", file=sys.stderr)
        return 1

    print(
        f"UART stage1: fixed uncached RAM entry 0x{args.load_address:08x}; "
        f"size=0x{size:x}; direct calls remain inside the stage"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
