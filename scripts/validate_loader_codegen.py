#!/usr/bin/env python3
"""Validate the GCC 10 data-free pre-DDR loader code-generation model."""
from __future__ import annotations

import argparse
import re
import subprocess
import sys
from dataclasses import dataclass
from pathlib import Path

FORBIDDEN_ELF_SECTIONS = {
    ".dynamic", ".dynsym", ".rel.dyn", ".rela.dyn", ".plt", ".interp",
    ".got", ".got.plt", ".data", ".sdata", ".bss", ".sbss",
    ".forbidden_loader_data",
}
ALLOWED_PRE_DDR_ALLOCATED = {".text", ".reginfo", ".MIPS.abiflags", ".pdr"}

@dataclass(frozen=True)
class Section:
    name: str
    size: int
    flags: str

@dataclass(frozen=True)
class Relocation:
    kind: str
    symbol: str


def run(tool: str, *args: str) -> str:
    result = subprocess.run([tool, *args], check=False, text=True, capture_output=True)
    if result.returncode:
        raise RuntimeError(f"{tool} {' '.join(args)} failed:\n{result.stderr}")
    return result.stdout


def text_relocations(readelf: str, obj: Path) -> list[Relocation]:
    output = run(readelf, "-rW", str(obj))
    section = ""
    relocs: list[Relocation] = []
    for line in output.splitlines():
        match = re.match(r"Relocation section '([^']+)'", line)
        if match:
            section = match.group(1)
            continue
        if section not in {".rel.text", ".rela.text"}:
            continue
        match = re.search(r"\b(R_MIPS_[A-Z0-9_]+)\b\s+[0-9a-fA-F]+\s+(.+?)\s*$", line)
        if match:
            relocs.append(Relocation(match.group(1), match.group(2).split()[0]))
    return relocs


def object_sections(readelf: str, path: Path) -> list[Section]:
    output = run(readelf, "-SW", str(path))
    sections: list[Section] = []
    pattern = re.compile(
        r"^\s*\[\s*\d+\]\s+(\S+)\s+\S+\s+[0-9a-fA-F]+\s+[0-9a-fA-F]+\s+"
        r"([0-9a-fA-F]+)\s+[0-9a-fA-F]+\s+(\S*)"
    )
    for line in output.splitlines():
        match = pattern.match(line)
        if match:
            sections.append(Section(match.group(1), int(match.group(2), 16), match.group(3)))
    return sections


def defined_global_text_symbols(readelf: str, obj: Path) -> list[str]:
    output = run(readelf, "-sW", str(obj))
    symbols: list[str] = []
    for line in output.splitlines():
        fields = line.split()
        if len(fields) < 8 or not fields[0].rstrip(":").isdigit():
            continue
        _value, _size, sym_type, bind, _vis, ndx, name = fields[1:8]
        if bind in {"GLOBAL", "WEAK"} and ndx != "UND" and sym_type in {"FUNC", "NOTYPE"}:
            symbols.append(name)
    return symbols


def undefined_symbols(readelf: str, obj: Path) -> list[str]:
    output = run(readelf, "-sW", str(obj))
    symbols: list[str] = []
    for line in output.splitlines():
        fields = line.split()
        if len(fields) < 8 or not fields[0].rstrip(":").isdigit():
            continue
        bind, ndx, name = fields[4], fields[6], fields[7]
        if ndx == "UND" and bind in {"GLOBAL", "WEAK"} and name:
            symbols.append(name)
    return symbols


def validate_expected_entries(readelf: str, obj: Path, expected_entries: list[str]) -> None:
    symbols = defined_global_text_symbols(readelf, obj)
    if symbols != expected_entries:
        raise ValueError(
            f"{obj}: expected stage entries {expected_entries!r}; found {symbols!r}. "
            "Private helpers must inline into their owning stage."
        )
    unresolved = undefined_symbols(readelf, obj)
    if unresolved:
        raise ValueError(f"{obj}: unresolved symbols are forbidden: {', '.join(unresolved)}")


def validate_pre_ddr_object(objdump: str, readelf: str, obj: Path,
                            expected_entries: list[str]) -> None:
    disassembly = run(objdump, "-drw", str(obj))
    stack = [line.strip() for line in disassembly.splitlines()
             if re.search(r"\bsp\b|\$29\b", line)]
    if stack:
        raise ValueError(
            f"{obj}: pre-DDR stage references the stack pointer:\n" +
            "\n".join(stack[:12])
        )
    calls = [line.strip() for line in disassembly.splitlines()
             if re.search(r"\b(?:j|jal|jalr)\s+", line)]
    if calls:
        raise ValueError(
            f"{obj}: pre-DDR C must be leaf-only; found jump/call instructions:\n" +
            "\n".join(calls[:12])
        )
    validate_expected_entries(readelf, obj, expected_entries)
    relocs = text_relocations(readelf, obj)
    if relocs:
        detail = ", ".join(f"{r.kind}:{r.symbol}" for r in relocs)
        raise ValueError(
            f"{obj}: data-free pre-DDR C must have no text relocations; found {detail}"
        )
    bad_sections = []
    for section in object_sections(readelf, obj):
        if section.size == 0 or "A" not in section.flags:
            continue
        if section.name not in ALLOWED_PRE_DDR_ALLOCATED:
            bad_sections.append(f"{section.name}=0x{section.size:x}")
    if bad_sections:
        raise ValueError(
            f"{obj}: pre-DDR C allocated data/GOT/literals: {', '.join(bad_sections)}"
        )


def section_names(readelf: str, elf: Path) -> set[str]:
    return {section.name for section in object_sections(readelf, elf)}


def describe_object(readelf: str, obj: Path) -> list[str]:
    relocs = text_relocations(readelf, obj)
    allocated = [f"{s.name}:0x{s.size:x}" for s in object_sections(readelf, obj)
                 if s.size and "A" in s.flags]
    return [
        f"object={obj}",
        "  text_relocations=" + (",".join(f"{r.kind}:{r.symbol}" for r in relocs) or "none"),
        "  global_text_symbols=" + (",".join(defined_global_text_symbols(readelf, obj)) or "none"),
        "  unresolved_symbols=" + (",".join(undefined_symbols(readelf, obj)) or "none"),
        "  allocated_sections=" + (",".join(allocated) or "none"),
    ]


def parse_entry_spec(value: str) -> tuple[Path, list[str]]:
    try:
        path, entries_text = value.rsplit(":", 1)
    except ValueError as exc:
        raise argparse.ArgumentTypeError("expected OBJECT:ENTRY[,ENTRY...]") from exc
    entries = [entry.strip() for entry in entries_text.split(",") if entry.strip()]
    if not path or not entries:
        raise argparse.ArgumentTypeError("expected OBJECT:ENTRY[,ENTRY...]")
    return Path(path), entries


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--elf", type=Path, required=True)
    parser.add_argument("--c-object", type=Path, action="append", default=[])
    parser.add_argument("--pre-ddr-object", type=parse_entry_spec, action="append", default=[])
    parser.add_argument("--relocatable-entry", type=parse_entry_spec, action="append", default=[])
    parser.add_argument("--objdump", required=True)
    parser.add_argument("--readelf", required=True)
    parser.add_argument("--nm", required=True)
    parser.add_argument("--report", type=Path)
    args = parser.parse_args()

    report = ["postmerkOS VCore-III GCC 10 data-free early-init report"]
    objects = args.c_object + [x[0] for x in args.pre_ddr_object]
    for obj in dict.fromkeys(objects):
        report.extend(describe_object(args.readelf, obj))

    try:
        for obj, entries in args.pre_ddr_object:
            validate_pre_ddr_object(args.objdump, args.readelf, obj, entries)
        sections = section_names(args.readelf, args.elf)
        forbidden = sorted(sections & FORBIDDEN_ELF_SECTIONS)
        if forbidden:
            raise ValueError(f"{args.elf}: forbidden loader sections: {', '.join(forbidden)}")
        if ".loader" not in sections:
            raise ValueError(f"{args.elf}: missing .loader output section")
        report.append("final_elf_sections=" + ",".join(sorted(sections)))
        report.append("status=PASS")
    except (OSError, RuntimeError, ValueError) as exc:
        report.append(f"status=FAIL: {exc}")
        if args.report:
            args.report.parent.mkdir(parents=True, exist_ok=True)
            args.report.write_text("\n".join(report) + "\n")
        print(f"loader-codegen validation failed: {exc}", file=sys.stderr)
        return 1

    if args.report:
        args.report.parent.mkdir(parents=True, exist_ok=True)
        args.report.write_text("\n".join(report) + "\n")
    print("loader code generation: pre-DDR C is data-free, call-free, stackless, and GOT-free")
    return 0

if __name__ == "__main__":
    raise SystemExit(main())
