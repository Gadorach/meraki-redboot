#!/usr/bin/env python3
"""Validate the pinned legacy-PIC LinuxLoader code-generation contract."""

from __future__ import annotations

import argparse
import re
import subprocess
import sys
from pathlib import Path

FORBIDDEN_ELF_SECTIONS = {
    ".interp",
    ".dynamic",
    ".dynsym",
    ".dynstr",
    ".rel.dyn",
    ".rela.dyn",
    ".plt",
    ".got.plt",
}


def run(tool: str, *args: str) -> str:
    result = subprocess.run([tool, *args], check=False, text=True, capture_output=True)
    if result.returncode:
        raise RuntimeError(f"{tool} {' '.join(args)} failed:\n{result.stderr}")
    return result.stdout


def validate_pre_ddr_object(objdump: str, obj: Path) -> None:
    disassembly = run(objdump, "-drw", str(obj))
    stack_offenders: list[str] = []
    call_offenders: list[str] = []
    absolute_jump_offenders: list[str] = []
    call_mnemonics = {"jal", "jalr", "bal", "bgezal", "bltzal"}
    for line in disassembly.splitlines():
        # Restrict checks to disassembled instruction lines so symbol names
        # cannot create false positives.
        if not re.match(r"^\s*[0-9a-fA-F]+:\s", line):
            continue
        stripped = line.strip()
        if re.search(r"(?:\$29\b|\bsp\b)", line):
            stack_offenders.append(stripped)
        # GNU and LLVM objdump use different whitespace/byte formatting. Skip
        # hexadecimal instruction-byte/word tokens and treat the first
        # remaining token as the mnemonic.
        instruction_tokens = line.split(":", 1)[1].strip().split()
        while instruction_tokens and re.fullmatch(
            r"(?:[0-9a-fA-F]{2}|[0-9a-fA-F]{4}|[0-9a-fA-F]{8})",
            instruction_tokens[0],
        ):
            instruction_tokens.pop(0)
        if instruction_tokens and instruction_tokens[0] in call_mnemonics:
            call_offenders.append(stripped)
        if instruction_tokens and instruction_tokens[0] == "j":
            absolute_jump_offenders.append(stripped)
    problems: list[str] = []
    if stack_offenders:
        excerpt = "\n".join(stack_offenders[:24])
        problems.append(
            f"pre-DDR initialization references the stack pointer:\n{excerpt}\n"
            "No writable stack exists until init_system_* returns."
        )
    if call_offenders:
        excerpt = "\n".join(call_offenders[:24])
        problems.append(
            f"pre-DDR initialization still contains call/link instructions:\n{excerpt}\n"
            "A nested call overwrites the assembly caller's return address or forces an ABI frame."
        )
    if absolute_jump_offenders:
        excerpt = "\n".join(absolute_jump_offenders[:24])
        problems.append(
            f"pre-DDR initialization contains absolute J instructions:\n{excerpt}\n"
            "MIPS J targets do not follow the active/fallback loader's runtime offset."
        )
    if problems:
        raise ValueError(
            f"{obj}: " + "\n\n".join(problems) +
            "\nThe reset-time inline/register contract was not honored; do not bypass this check. "
            "Inspect the adjacent .s, .dis and .relocs files under .work/build/."
        )


def section_names(readelf: str, elf: Path) -> set[str]:
    output = run(readelf, "-SW", str(elf))
    names: set[str] = set()
    for line in output.splitlines():
        match = re.match(r"\s*\[\s*\d+\]\s+(\S+)", line)
        if match:
            names.add(match.group(1))
    return names


def validate_no_unresolved(nm: str, elf: Path) -> None:
    output = run(nm, "-u", str(elf)).strip()
    if output:
        raise ValueError(f"{elf}: unresolved symbols remain:\n{output}")


def validate_final_relocations(readelf: str, elf: Path) -> None:
    output = run(readelf, "-rW", str(elf))
    relocs = re.findall(r"\bR_MIPS_[A-Z0-9_]+\b", output)
    if relocs:
        unique = ", ".join(sorted(set(relocs)))
        raise ValueError(f"{elf}: final linked loader retains relocations: {unique}")


def validate_entry(readelf: str, elf: Path) -> None:
    output = run(readelf, "-hW", str(elf))
    match = re.search(r"Entry point address:\s*(0x[0-9a-fA-F]+)", output)
    if not match or int(match.group(1), 16) != 0:
        raise ValueError(f"{elf}: loader entry point must be link address zero")




def validate_uart_embedding(nm: str, elf: Path) -> None:
    data = elf.read_bytes()
    if b"PMOSRAM READY 2" not in data:
        return
    output = run(nm, "-n", str(elf))
    names = {line.split()[-1] for line in output.splitlines() if line.split()}
    for required in ("uart_stage1_blob_start", "uart_stage1_blob_end"):
        if required not in names:
            raise ValueError(f"embedded UART stage1 is missing symbol {required}")
    if "uart_ramloader_probe_and_run" in names:
        raise ValueError("ordinary UART C was linked into relocatable flash code")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--elf", type=Path, required=True)
    parser.add_argument("--pre-ddr-object", type=Path, action="append", default=[])
    parser.add_argument("--objdump", required=True)
    parser.add_argument("--readelf", required=True)
    parser.add_argument("--nm", required=True)
    args = parser.parse_args()

    try:
        for obj in args.pre_ddr_object:
            validate_pre_ddr_object(args.objdump, obj)
        sections = section_names(args.readelf, args.elf)
        forbidden = sorted(sections & FORBIDDEN_ELF_SECTIONS)
        if forbidden:
            raise ValueError(f"{args.elf}: forbidden dynamic/ABI sections: {', '.join(forbidden)}")
        validate_no_unresolved(args.nm, args.elf)
        validate_final_relocations(args.readelf, args.elf)
        validate_entry(args.readelf, args.elf)
        validate_uart_embedding(args.nm, args.elf)
    except (OSError, RuntimeError, ValueError) as exc:
        print(f"loader-codegen validation failed: {exc}", file=sys.stderr)
        return 1

    print("loader code generation: pre-DDR init is stackless; flash UART control flow is assembly-only; UART C is isolated in fixed RAM")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
