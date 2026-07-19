#!/usr/bin/env python3
from __future__ import annotations

import argparse
import struct
from pathlib import Path

BOOT_SIZE = 0x40000
VECTOR_OFFSETS = (0x000, 0x080, 0x100, 0x180, 0x200)
FALLBACK_DESC = 0x41C
ACTIVE_DESC = 0x444


def die(message: str) -> None:
    raise SystemExit(f"validation failed: {message}")


def u32(data: bytes, off: int) -> int:
    return struct.unpack_from("<I", data, off)[0]


def decode_desc(data: bytes, off: int) -> tuple[int, int]:
    lui = u32(data, off)
    ori = u32(data, off + 4)
    if lui >> 26 != 0x0F or ori >> 26 != 0x0D:
        die(f"descriptor at 0x{off:x} does not begin with LUI/ORI")
    pointer = ((lui & 0xFFFF) << 16) | (ori & 0xFFFF)
    length = u32(data, off + 0x14)
    return pointer, length


def symbols(path: Path) -> dict[str, int]:
    result: dict[str, int] = {}
    for line in path.read_text().splitlines():
        parts = line.split()
        if len(parts) >= 3:
            try:
                value = int(parts[0], 16)
            except ValueError:
                continue
            result[parts[-1]] = value
    return result


def branch_kind(word: int) -> tuple[int, int, int]:
    return (word >> 26, (word >> 21) & 0x1F, (word >> 16) & 0x1F)


def require_only_policy_symbol(
    syms: dict[str, int], prefix: str, selected: str, all_names: tuple[str, ...]
) -> None:
    expected = f"{prefix}{selected}"
    if expected not in syms:
        die(f"missing selected policy marker {expected}")
    unexpected = [f"{prefix}{name}" for name in all_names if name != selected and f"{prefix}{name}" in syms]
    if unexpected:
        die(f"unexpected policy marker(s): {', '.join(unexpected)}")


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--variant", required=True)
    ap.add_argument("--crc-policy", choices=("strict", "warn", "off"), required=True)
    ap.add_argument(
        "--size-policy", choices=("legacy-strict", "legacy-warn", "hard-only"), required=True
    )
    ap.add_argument("--loader", type=Path, required=True)
    ap.add_argument("--image", type=Path, required=True)
    ap.add_argument("--symbols", type=Path, required=True)
    ap.add_argument("--shared-stage", type=Path)
    ap.add_argument("--shared-stage-offset", type=lambda value: int(value, 0))
    args = ap.parse_args()

    loader = args.loader.read_bytes()
    image = args.image.read_bytes()
    if len(image) != BOOT_SIZE:
        die(f"image size is {len(image)}, expected {BOOT_SIZE}")
    if len(loader) == 0 or len(loader) % 4:
        die("loader must be non-empty and word aligned")
    if loader[:4] != bytes.fromhex("01001104"):
        die("loader does not begin with expected little-endian BAL instruction")

    for reason, off in enumerate(VECTOR_OFFSETS):
        words = struct.unpack_from("<4I", image, off)
        expected_prefix = (0x035AD026, 0x375A0000 | reason)
        if words[:2] != expected_prefix or words[3] != 0:
            die(f"bad reset/exception vector at 0x{off:x}")
        bal = words[2]
        opcode = bal >> 26
        rt = (bal >> 16) & 0x1F
        immediate = bal & 0xFFFF
        if immediate & 0x8000:
            immediate -= 0x10000
        target = off + 8 + 4 + (immediate << 2)
        if opcode != 0x01 or rt != 0x11 or target != 0x400:
            die(f"vector at 0x{off:x} does not BAL to dispatcher 0x400")

    expected_wrapper = {
        0x400: 0x3C1BFFFF,
        0x404: 0x377BFC00,
        0x408: 0x037FF824,
        0x40C: 0x10000000,
        0x414: 0x10000007,
        0x434: 0x10000003,
        0x43C: 0x10000007,
    }
    for off, expected in expected_wrapper.items():
        if u32(image, off) != expected:
            die(f"wrapper instruction mismatch at 0x{off:x}")

    fallback, flen = decode_desc(image, FALLBACK_DESC)
    active, alen = decode_desc(image, ACTIVE_DESC)
    if flen != len(loader) or alen != len(loader):
        die("descriptor length does not equal compiled loader length")
    syms = symbols(args.symbols)
    uart_stage_present = "loader_uart_stage1_copy_text" in syms
    if uart_stage_present:
        if args.shared_stage is None or args.shared_stage_offset is None:
            die("UART-enabled loader validation requires the shared stage binary and offset")
        stage = args.shared_stage.read_bytes()
        stage_offset = args.shared_stage_offset
        if fallback + flen != stage_offset:
            die("fallback copy does not end at the shared UART stage boundary")
        if image[stage_offset : stage_offset + len(stage)] != stage:
            die("shared UART stage differs from uart-stage1.bin")
        if stage_offset + len(stage) > BOOT_SIZE:
            die("shared UART stage exceeds the boot region")
    else:
        if args.shared_stage is not None or args.shared_stage_offset is not None:
            die("non-UART loader unexpectedly supplied shared-stage metadata")
        stage = b""
        stage_offset = BOOT_SIZE
        if fallback + flen != BOOT_SIZE:
            die("fallback copy does not end at 0x40000")
    if active + alen != fallback:
        die("active and fallback copies are not contiguous")
    if image[active : active + alen] != loader:
        die("active embedded copy differs from loader.bin")
    if image[fallback : fallback + flen] != loader:
        die("fallback embedded copy differs from loader.bin")

    if "loader_hard_size_reject_branch" not in syms:
        die("missing mandatory hard-size rejection symbol")
    hard_word = u32(loader, syms["loader_hard_size_reject_branch"])
    if branch_kind(hard_word) != (5, 8, 0):
        die(f"hard-size instruction is not BNEZ t0: 0x{hard_word:08x}")

    crc_suffix = {"strict": "strict", "warn": "warn", "off": "off"}[args.crc_policy]
    require_only_policy_symbol(
        syms,
        "loader_crc_policy_",
        crc_suffix,
        ("strict", "warn", "off"),
    )
    size_suffix = {
        "legacy-strict": "legacy_strict",
        "legacy-warn": "legacy_warn",
        "hard-only": "hard_only",
    }[args.size_policy]
    require_only_policy_symbol(
        syms,
        "loader_size_policy_",
        size_suffix,
        ("legacy_strict", "legacy_warn", "hard_only"),
    )

    if args.crc_policy == "off":
        if "crctab" in syms:
            die("CRC-off loader still contains the CRC table")
    elif "crctab" not in syms:
        die("CRC-enabled loader is missing the CRC table")

    warnings_expected = args.crc_policy == "warn" or args.size_policy == "legacy-warn"
    writer_expected = warnings_expected or uart_stage_present
    if writer_expected and "loader_uart_puts" not in syms:
        die("assembly UART writer is required by warnings or the UART stage shim")
    if not writer_expected and "loader_uart_puts" in syms:
        die("assembly UART writer was retained without warnings or UART stage support")

    print(
        f"validated {args.variant}: crc={args.crc_policy} size={args.size_policy} "
        f"loader={len(loader)} bytes image={len(image)} bytes"
    )
    print(f"  active:   0x{active:05x}-0x{active + alen - 1:05x}")
    print(f"  fallback: 0x{fallback:05x}-0x{fallback + flen - 1:05x}")
    if uart_stage_present:
        print(f"  stage1:   0x{stage_offset:05x}-0x{stage_offset + len(stage) - 1:05x}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
