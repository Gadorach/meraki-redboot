#!/usr/bin/env python3
from __future__ import annotations

import argparse


def parse_int(value: str) -> int:
    return int(value, 0)


def bool_value(value: str) -> bool:
    return value.lower() in {"1", "y", "yes", "true", "on", "enabled"}


def main() -> int:
    ap = argparse.ArgumentParser(description="Validate LinuxLoader policy and memory geometry")
    ap.add_argument("--crc-policy", choices=("strict", "warn", "off"), required=True)
    ap.add_argument("--size-policy", choices=("legacy-strict", "legacy-warn", "hard-only"), required=True)
    ap.add_argument("--fallback-region-size", type=parse_int, required=True)
    ap.add_argument("--loader-region-size", type=parse_int, default=0x40000)
    ap.add_argument("--payload-slot-end", type=parse_int, required=True)
    ap.add_argument("--legacy-payload-limit", type=parse_int, required=True)
    ap.add_argument("--hard-payload-limit", type=parse_int, required=True)
    ap.add_argument("--uart-ramloader", required=True)
    ap.add_argument("--uart-max-size", type=parse_int, required=True)
    ap.add_argument("--uart-ram-start", type=parse_int, required=True)
    ap.add_argument("--uart-ram-end", type=parse_int, required=True)
    ap.add_argument("--uart-probe-timeout-ms", type=parse_int, required=True)
    ap.add_argument("--uart-interbyte-timeout-ms", type=parse_int, required=True)
    ap.add_argument("--uart-count-hz", type=parse_int, required=True)
    ap.add_argument("--uart-stage1-addr", type=parse_int, required=True)
    ap.add_argument("--uart-stage1-max-size", type=parse_int, required=True)
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
        ap.error(f"hard payload limit must be in 1..0x{capacity:x}")
    if not (0 < args.legacy_payload_limit <= args.hard_payload_limit):
        ap.error("legacy payload limit must be positive and no larger than the hard payload limit")
    if args.hard_payload_limit % 32 or args.legacy_payload_limit % 32:
        ap.error("payload limits must be 32-byte aligned")

    if not (0x80000000 <= args.uart_ram_start < args.uart_ram_end <= 0xA0000000):
        ap.error("UART RAM range must be a valid KSEG0 interval")
    if args.uart_max_size <= 0 or args.uart_max_size > args.uart_ram_end - args.uart_ram_start:
        ap.error("UART maximum payload size does not fit the configured RAM range")
    if not (100 <= args.uart_probe_timeout_ms <= 10000):
        ap.error("UART probe timeout must be from 100 through 10000 ms")
    if not (100 <= args.uart_interbyte_timeout_ms <= 10000):
        ap.error("UART inter-byte timeout must be from 100 through 10000 ms")
    if args.uart_count_hz < 1_000_000:
        ap.error("UART CP0 Count frequency is implausibly low")
    if bool_value(args.uart_ramloader):
        if not (0xA0000000 <= args.uart_stage1_addr < 0xC0000000):
            ap.error("UART stage1 must execute from an uncached KSEG1 address")
        expected_alias = args.uart_ram_end + 0x20000000
        if args.uart_stage1_addr != expected_alias:
            ap.error(
                "UART stage1 must begin at the uncached alias of uart-ram-end "
                f"(expected 0x{expected_alias:x})"
            )
        if not (0 < args.uart_stage1_max_size <= 0x00100000):
            ap.error("UART stage1 reserved size must be in 1..0x100000")
        if args.uart_stage1_addr + args.uart_stage1_max_size > 0xA8000000:
            ap.error("UART stage1 exceeds the 128 MiB DRAM top boundary")

    print(
        "configuration: "
        f"crc={args.crc_policy} size={args.size_policy} "
        f"fallback-region=0x{args.fallback_region_size:x} slot-end=0x{args.payload_slot_end:x} "
        f"legacy=0x{args.legacy_payload_limit:x} hard=0x{args.hard_payload_limit:x} "
        f"uart={'enabled' if bool_value(args.uart_ramloader) else 'disabled'} "
        f"uart-ram=0x{args.uart_ram_start:x}-0x{args.uart_ram_end:x} "
        f"stage1=0x{args.uart_stage1_addr:x}+0x{args.uart_stage1_max_size:x}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
