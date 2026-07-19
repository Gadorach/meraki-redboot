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
    parser.add_argument("--binary", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    data = args.binary.read_bytes()
    marker = f"PMOSRECOVERY3;SOC={args.family};FAMILY={args.family_id};SPI={args.spi_address:08x};PROTO=3;PREFLIGHT=4;BAUDTEST=1;FRAME_MAX=4096;WINDOW_MAX=16;ACKFMT=BIN1;SPARSE=1;LZ4=1;CONFIRM_RETRY=1;AUTO_CONFIRM=1;AUTO_REBOOT=1;END".encode()
    if marker not in data:
        raise SystemExit("compiled payload does not contain its machine-readable descriptor")
    descriptor = {
        "format": "postmerkos.uart-recovery-payload.v3",
        "protocol_version": 3,
        "soc_family": args.family,
        "soc_family_id": args.family_id,
        "spi_software_mode_address": args.spi_address,
        "accepted_models": MODELS[args.family],
        "accepted_flash_bytes": 16 * 1024 * 1024,
        "accepted_jedec_ids": ["c22018", "ef4018", "012018", "20ba18", "c84018"],
        "flash_geometry": {
            "bytes": 16 * 1024 * 1024,
            "erase_bytes": 64 * 1024,
            "page_bytes": 256,
            "address_bytes": 3,
        },
        "operations": ["verify", "preflight", "dry-run", "flash"],
        "transport_integrity": ["frame-crc32", "compact-ack-crc32", "object-crc32", "object-sha256", "reconstructed-image-sha256"],
        "adaptive_transport_contract": "pmosrec-v3-adaptive-uart-sparse-lz4-v1",
        "adaptive_transport": {
            "stable_bootstrap_baud": 115200,
            "target_divisor_negotiation": True,
            "bidirectional_prng_crc_test": True,
            "autonomous_baud_fallback": True,
            "baud_refinement_percent": 2,
            "frame_sizes": [1024, 4096],
            "maximum_window_frames": 16,
            "ack_format": "binary-cumulative-selective-retry-v1",
            "manifest_first": True,
            "representations": ["raw", "sparse", "lz4", "sparse-lz4"],
            "confirmation_retry": "infinite",
            "automatic_reboot_seconds": 5,
        },
        "load_address": 0x86C00000,
        "entry_address": 0x86C00000,
        "entry_contract": "flat-binary-byte-zero-v1",
        "manifest_lookup_contract": "direct-object-members-v1",
        "hardware_preflight_contract": "spi-nor-scratch-rw-restore-loader-crc-v4",
        "spi_master_enable_contract": "preserve-general-ctrl-enable-spi-v1",
        "preflight_scratch": {
            "default_address": 0x00FF0000,
            "bytes": 64 * 1024,
            "minimum_address": 0x00040000,
            "restore_original": True,
        },
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
