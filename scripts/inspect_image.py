#!/usr/bin/env python3
from __future__ import annotations
import argparse, hashlib, struct
from pathlib import Path


def u32(data: bytes, off: int) -> int:
    return struct.unpack_from("<I", data, off)[0]


def desc(data: bytes, off: int) -> tuple[int, int]:
    return (((u32(data, off) & 0xffff) << 16) | (u32(data, off + 4) & 0xffff), u32(data, off + 0x14))


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--loader", type=Path, required=True)
    ap.add_argument("--image", type=Path, required=True)
    ap.add_argument("--symbols", type=Path, required=True)
    a = ap.parse_args()
    loader, image = a.loader.read_bytes(), a.image.read_bytes()
    active = desc(image, 0x444)
    fallback = desc(image, 0x41c)
    print(f"loader:   {a.loader} ({len(loader)} bytes) sha256={hashlib.sha256(loader).hexdigest()}")
    print(f"image:    {a.image} ({len(image)} bytes) sha256={hashlib.sha256(image).hexdigest()}")
    print(f"active:   start=0x{active[0]:05x} length=0x{active[1]:x}")
    print(f"fallback: start=0x{fallback[0]:05x} length=0x{fallback[1]:x}")
    interesting = (
        "loader_hard_size_reject_branch",
        "loader_size_policy_legacy_strict",
        "loader_size_policy_legacy_warn",
        "loader_size_policy_hard_only",
        "loader_crc_policy_strict",
        "loader_crc_policy_warn",
        "loader_crc_policy_off",
        "loader_uart_puts",
        "crctab",
    )
    for line in a.symbols.read_text().splitlines():
        if any(line.endswith(name) for name in interesting):
            print(f"symbol:   {line}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
