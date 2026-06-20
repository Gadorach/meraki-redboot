#!/usr/bin/env python3
from __future__ import annotations
import argparse
import hashlib
import json
from pathlib import Path

MODELS = {
    "luton26": ["MS22", "MS22P", "MS220-8", "MS220-8P", "MS220-24", "MS220-24P"],
    "jaguar1": [
        "MS320-24", "MS320-24P", "MS220-48", "MS220-48P", "MS220-48LP",
        "MS220-48FP", "MS320-48", "MS320-48P", "MS320-48LP", "MS320-48FP",
        "MS42", "MS42P",
    ],
}


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--family", choices=sorted(MODELS), required=True)
    parser.add_argument("--family-id", type=lambda value: int(value, 0), required=True)
    parser.add_argument("--spi-address", type=lambda value: int(value, 0), required=True)
    parser.add_argument("--load-address", type=lambda value: int(value, 0), required=True)
    parser.add_argument("--entry-address", type=lambda value: int(value, 0), required=True)
    parser.add_argument("--binary", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    data = args.binary.read_bytes()
    marker = f"PMOSRECOVERY2;SOC={args.family};FAMILY={args.family_id};SPI={args.spi_address:08x};PROTO=2;END".encode()
    if marker not in data:
        raise SystemExit("compiled payload does not contain its machine-readable descriptor")
    descriptor = {
        "format": "postmerkos.uart-recovery-payload.v2",
        "protocol_version": 2,
        "soc_family": args.family,
        "soc_family_id": args.family_id,
        "spi_software_mode_address": args.spi_address,
        "load_address": args.load_address,
        "entry_address": args.entry_address,
        "accepted_models": MODELS[args.family],
        "accepted_flash_bytes": 16 * 1024 * 1024,
        "accepted_jedec_ids": ["c22018", "ef4018", "012018", "20ba18", "c84018"],
        "flash_geometry": {
            "bytes": 16 * 1024 * 1024,
            "erase_bytes": 64 * 1024,
            "page_bytes": 256,
            "address_bytes": 3,
        },
        "operations": ["verify", "dry-run", "flash"],
        "transport_integrity": ["frame-crc32", "object-crc32", "object-sha256"],
        "binary": {
            "filename": args.binary.name,
            "bytes": len(data),
            "sha256": hashlib.sha256(data).hexdigest(),
        },
    }
    args.output.write_text(json.dumps(descriptor, indent=2, sort_keys=True) + "\n")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
