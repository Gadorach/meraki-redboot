#!/usr/bin/env python3
"""Validate fixed-RAM stage 1 and its non-executable recovery blobs."""
from __future__ import annotations

import argparse
import re
import subprocess
import sys
from dataclasses import dataclass
from pathlib import Path

FORBIDDEN_SECTIONS = {
    ".interp", ".dynamic", ".dynsym", ".dynstr", ".rel.dyn", ".rela.dyn",
    ".plt", ".got", ".got.plt",
}


@dataclass(frozen=True)
class Section:
    name: str
    address: int
    offset: int
    size: int
    flags: str
    section_type: str


def integer(value: str) -> int:
    return int(value, 0)


def run(tool: str, *args: str) -> str:
    result = subprocess.run([tool, *args], text=True, capture_output=True, check=False)
    if result.returncode:
        raise RuntimeError(f"{tool} {' '.join(args)} failed:\n{result.stderr}")
    return result.stdout


def parse_sections(readelf: str, elf: Path) -> dict[str, Section]:
    output = run(readelf, "-SW", str(elf))
    result: dict[str, Section] = {}
    pattern = re.compile(
        r"^\s*\[\s*\d+\]\s+(\S+)\s+(\S+)\s+([0-9a-fA-F]+)\s+"
        r"([0-9a-fA-F]+)\s+([0-9a-fA-F]+)\s+[0-9a-fA-F]+\s+(\S*)"
    )
    for line in output.splitlines():
        match = pattern.match(line)
        if not match:
            continue
        name, section_type, address, offset, size, flags = match.groups()
        result[name] = Section(
            name=name,
            address=int(address, 16),
            offset=int(offset, 16),
            size=int(size, 16),
            flags=flags,
            section_type=section_type,
        )
    return result


def parse_symbols(nm: str, elf: Path) -> tuple[dict[str, int], list[str]]:
    symbols: dict[str, int] = {}
    undefined: list[str] = []
    for line in run(nm, "-n", str(elf)).splitlines():
        fields = line.split()
        if len(fields) >= 3:
            symbols[fields[-1]] = int(fields[0], 16) & 0xFFFFFFFF
        elif len(fields) >= 2 and fields[-2].upper() == "U":
            undefined.append(fields[-1])
    return symbols, undefined


def validate_calls(objdump: str, elf: Path, text: Section) -> None:
    # Restrict disassembly to executable stage code.  Embedded recovery images
    # are opaque data and must never be decoded as stage-1 instructions.
    output = run(objdump, "-drwC", "-j", text.name, str(elf))
    offenders: list[str] = []
    start = text.address
    end = text.address + text.size
    for line in output.splitlines():
        address_match = re.match(r"^\s*([0-9a-fA-F]+):\s", line)
        if not address_match:
            continue
        target_match = re.search(r"\b(?:j|jal)\s+(?:0x)?([0-9a-fA-F]+)\b", line)
        if not target_match:
            continue
        instruction_address = int(address_match.group(1), 16)
        encoded_target = int(target_match.group(1), 16)
        runtime_target = ((instruction_address + 4) & 0xF0000000) | (encoded_target & 0x0FFFFFFF)
        if not (start <= runtime_target < end):
            offenders.append(line.strip())
    if offenders:
        raise ValueError(
            "stage executable contains direct jump/call targets outside .text:\n"
            + "\n".join(offenders[:32])
        )


def compare_blob(
    elf_bytes: bytes,
    embedded: Section,
    symbols: dict[str, int],
    start_symbol: str,
    end_symbol: str,
    reference: Path,
) -> tuple[int, str]:
    if start_symbol not in symbols or end_symbol not in symbols:
        raise ValueError(f"missing embedded payload symbols {start_symbol}/{end_symbol}")
    start = symbols[start_symbol]
    end = symbols[end_symbol]
    if not (embedded.address <= start <= end <= embedded.address + embedded.size):
        raise ValueError(f"{start_symbol}/{end_symbol} lie outside .embedded_recovery")
    actual = elf_bytes[
        embedded.offset + (start - embedded.address):
        embedded.offset + (end - embedded.address)
    ]
    expected = reference.read_bytes()
    if actual != expected:
        raise ValueError(f"embedded bytes for {reference.name} do not match build artifact")
    import hashlib
    return len(actual), hashlib.sha256(actual).hexdigest()


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--elf", type=Path, required=True)
    ap.add_argument("--objdump", required=True)
    ap.add_argument("--readelf", required=True)
    ap.add_argument("--nm", required=True)
    ap.add_argument("--entry", type=integer, required=True)
    ap.add_argument("--maximum-size", type=integer, required=True)
    ap.add_argument("--range-end", type=integer, required=True)
    ap.add_argument("--recovery-luton26", type=Path, required=True)
    ap.add_argument("--recovery-jaguar1", type=Path, required=True)
    ap.add_argument("--report", type=Path)
    args = ap.parse_args()
    report: list[str] = [f"elf={args.elf}"]

    try:
        sections = parse_sections(args.readelf, args.elf)
        symbols, undefined = parse_symbols(args.nm, args.elf)
        if undefined:
            raise ValueError("unresolved symbols: " + ", ".join(sorted(set(undefined))))
        if symbols.get("uart_stage1_entry") != args.entry:
            raise ValueError(
                f"uart_stage1_entry must be 0x{args.entry:08x}; "
                f"found {symbols.get('uart_stage1_entry')!r}"
            )

        relocations = run(args.readelf, "-rW", str(args.elf))
        remaining = sorted(set(re.findall(r"\bR_MIPS_[A-Z0-9_]+\b", relocations)))
        if remaining:
            raise ValueError("final stage retains relocations: " + ", ".join(remaining))

        forbidden = sorted(set(sections) & FORBIDDEN_SECTIONS)
        if forbidden:
            raise ValueError("forbidden dynamic/GOT sections: " + ", ".join(forbidden))
        for required in (".text", ".rodata", ".embedded_recovery"):
            if required not in sections:
                raise ValueError(f"missing required output section {required}")
        text = sections[".text"]
        embedded = sections[".embedded_recovery"]
        if "X" not in text.flags:
            raise ValueError(".text is not executable")
        if "X" in embedded.flags:
            raise ValueError(".embedded_recovery must be non-executable")
        if text.address != args.entry:
            raise ValueError(f".text begins at 0x{text.address:08x}, expected 0x{args.entry:08x}")
        if embedded.address < text.address + text.size:
            raise ValueError("embedded recovery section overlaps executable stage code")

        allocated = [section for section in sections.values() if section.size and "A" in section.flags]
        if not allocated:
            raise ValueError("ELF has no allocated sections")
        allocated_start = min(section.address for section in allocated)
        allocated_end = max(section.address + section.size for section in allocated)
        if allocated_start != args.entry:
            raise ValueError(f"first allocated byte is 0x{allocated_start:08x}")
        image_size = allocated_end - allocated_start
        if image_size > args.maximum_size:
            raise ValueError(
                f"allocated image size 0x{image_size:x} exceeds maximum 0x{args.maximum_size:x}"
            )
        if allocated_end > args.range_end:
            raise ValueError(
                f"allocated image ends at 0x{allocated_end:08x}, beyond 0x{args.range_end:08x}"
            )
        bss = sections.get(".bss")
        if bss and bss.size:
            raise ValueError("fixed stage has a non-empty BSS")

        validate_calls(args.objdump, args.elf, text)
        elf_bytes = args.elf.read_bytes()
        luton_size, luton_sha = compare_blob(
            elf_bytes, embedded, symbols,
            "uart_recovery_luton26_start", "uart_recovery_luton26_end",
            args.recovery_luton26,
        )
        jaguar_size, jaguar_sha = compare_blob(
            elf_bytes, embedded, symbols,
            "uart_recovery_jaguar1_start", "uart_recovery_jaguar1_end",
            args.recovery_jaguar1,
        )
        if embedded.size < luton_size + jaguar_size:
            raise ValueError("embedded recovery section is smaller than its two payloads")

        for marker in (
            b"PMOSMENU SELECT",
            b"1=RAM-LOADER",
            b"2=FW-RECOVERY",
            b"PMOSREC CHAINLOAD",
            b"PMOSBOOT FLASH-FAIL",
        ):
            if marker not in elf_bytes:
                raise ValueError(f"stage lacks required marker {marker!r}")

        report.extend([
            f"entry=0x{args.entry:08x}",
            f"text_address=0x{text.address:08x}",
            f"text_size=0x{text.size:x}",
            f"embedded_address=0x{embedded.address:08x}",
            f"embedded_size=0x{embedded.size:x}",
            f"allocated_end=0x{allocated_end:08x}",
            f"allocated_size=0x{image_size:x}",
            f"luton26_size=0x{luton_size:x}",
            f"luton26_sha256={luton_sha}",
            f"jaguar1_size=0x{jaguar_size:x}",
            f"jaguar1_sha256={jaguar_sha}",
            "embedded_section_executable=no",
            "instruction_validation_section=.text",
            "runtime_relocations=none",
            "undefined_symbols=none",
            "result=pass",
        ])
    except (OSError, RuntimeError, ValueError) as exc:
        report.extend(["result=fail", f"error={exc}"])
        if args.report:
            args.report.parent.mkdir(parents=True, exist_ok=True)
            args.report.write_text("\n".join(report) + "\n")
        print(f"UART-stage1 validation failed: {exc}", file=sys.stderr)
        return 1

    if args.report:
        args.report.parent.mkdir(parents=True, exist_ok=True)
        args.report.write_text("\n".join(report) + "\n")
    print(
        f"UART stage1 verified at 0x{args.entry:08x}; code=0x{text.size:x}; "
        f"embedded=0x{embedded.size:x}; total=0x{image_size:x}; recovery data is non-executable"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
