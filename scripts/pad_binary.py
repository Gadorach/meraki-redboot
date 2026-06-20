#!/usr/bin/env python3
"""Pad a binary in place to a requested byte alignment."""
from __future__ import annotations
import argparse
from pathlib import Path


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("path", type=Path)
    parser.add_argument("--alignment", type=int, default=4)
    args = parser.parse_args()
    if args.alignment <= 0 or args.alignment & (args.alignment - 1):
        parser.error("alignment must be a positive power of two")
    data = args.path.read_bytes()
    padding = (-len(data)) & (args.alignment - 1)
    if padding:
        args.path.write_bytes(data + bytes(padding))
    print(f"padded {args.path} by {padding} byte(s) to {len(data) + padding}")
    return 0

if __name__ == "__main__":
    raise SystemExit(main())
