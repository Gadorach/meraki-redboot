#!/usr/bin/env python3
from __future__ import annotations

import argparse


def parse_int(value: str) -> int:
    return int(value, 0)


def main() -> int:
    ap = argparse.ArgumentParser(description="Validate LinuxLoader policy and flash geometry")
    ap.add_argument("--crc-policy", choices=("strict", "warn", "off"), required=True)
    ap.add_argument("--size-policy", choices=("legacy-strict", "legacy-warn", "hard-only"), required=True)
    ap.add_argument("--fallback-region-size", type=parse_int, required=True)
    ap.add_argument("--loader-region-size", type=parse_int, default=0x40000)
    ap.add_argument("--payload-slot-end", type=parse_int, required=True)
    ap.add_argument("--legacy-payload-limit", type=parse_int, required=True)
    ap.add_argument("--hard-payload-limit", type=parse_int, required=True)
    args = ap.parse_args()

    if args.fallback_region_size <= 0 or args.fallback_region_size & (args.fallback_region_size - 1):
        ap.error("fallback-region-size must be a positive power of two")
    if args.loader_region_size != 0x40000:
        ap.error("this wrapper requires loader-region-size 0x40000")
    payload_header_size = 32
    if args.payload_slot_end > args.fallback_region_size:
        ap.error("payload slot end may not cross the next fallback region")
    capacity = args.payload_slot_end - args.loader_region_size - payload_header_size
    if capacity <= 0:
        ap.error("payload slot must leave room for the loader and 32-byte payload header")
    if not (0 < args.hard_payload_limit <= capacity):
        ap.error(
            f"hard payload limit must be in 1..0x{capacity:x}; payload plus its 32-byte header may not cross the payload slot boundary"
        )
    if not (0 < args.legacy_payload_limit <= args.hard_payload_limit):
        ap.error("legacy payload limit must be positive and no larger than the hard payload limit")
    if args.hard_payload_limit % 32:
        ap.error("hard payload limit must be 32-byte aligned")
    if args.legacy_payload_limit % 32:
        ap.error("legacy payload limit must be 32-byte aligned")

    print(
        "configuration: "
        f"crc={args.crc_policy} size={args.size_policy} "
        f"fallback-region=0x{args.fallback_region_size:x} slot-end=0x{args.payload_slot_end:x} "
        f"legacy=0x{args.legacy_payload_limit:x} "
        f"hard=0x{args.hard_payload_limit:x}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
