#!/usr/bin/env python3
"""Describe the Meraki 256 KiB boot-region wrapper without using it as input."""
from __future__ import annotations

import argparse
import hashlib
import struct
from pathlib import Path

BOOT_SIZE = 0x40000


def u32(data: bytes, offset: int) -> int:
    return struct.unpack_from("<I", data, offset)[0]


def descriptor(data: bytes, offset: int) -> tuple[int, int]:
    lui = u32(data, offset)
    ori = u32(data, offset + 4)
    if lui >> 26 != 0x0F or ori >> 26 != 0x0D:
        raise ValueError(f"descriptor at 0x{offset:x} is not LUI/ORI")
    start = ((lui & 0xFFFF) << 16) | (ori & 0xFFFF)
    length = u32(data, offset + 0x14)
    return start, length


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("image", type=Path)
    args = parser.parse_args()
    data = args.image.read_bytes()
    if len(data) != BOOT_SIZE:
        raise SystemExit(f"expected a 256 KiB image, got {len(data)} bytes")

    fallback = descriptor(data, 0x41C)
    active = descriptor(data, 0x444)
    print(f"image: {args.image}")
    print(f"sha256: {hashlib.sha256(data).hexdigest()}")
    for name, (start, length) in (("active", active), ("fallback", fallback)):
        end = start + length
        payload = data[start:end]
        print(
            f"{name}: start=0x{start:05x} length=0x{length:x} "
            f"end=0x{end:05x} sha256={hashlib.sha256(payload).hexdigest()}"
        )
    print("selector words:")
    for offset in (0x400, 0x404, 0x408, 0x40C, 0x414, 0x434, 0x43C):
        print(f"  0x{offset:03x}: 0x{u32(data, offset):08x}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
