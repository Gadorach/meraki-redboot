#!/usr/bin/env python3
from __future__ import annotations
import argparse
import hashlib
import json
from pathlib import Path

FAMILIES = {
    "luton26": {
        "id": 1,
        "models": ["MS22", "MS22P", "MS220-8", "MS220-8P", "MS220-24", "MS220-24P"],
    },
    "jaguar1": {"id": 2, "models": ["MS42", "MS42P"]},
}


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--family", choices=sorted(FAMILIES), required=True)
    parser.add_argument("--binary", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    target = FAMILIES[args.family]
    marker = (
        f"PMOSLIVE3;SOC={args.family};FAMILY={target['id']};PROTO=3;FLASH=0;LIVEBOOT=1;"
        "IMAGE_BYTES=16777216;KERNEL=81000000;ROOTFS=87000000;MEM_MIB=120;"
        "FRAME_MAX=4096;WINDOW_MAX=16;SPARSE=1;LZ4=1;END"
    ).encode()
    data = args.binary.read_bytes()
    if marker not in data:
        raise SystemExit("compiled payload does not contain the expected PMOSLIVE descriptor")
    forbidden = [b"ERASEFLASH", b"FLASH-PREFLIGHT", b"PROGRESS ERASE", b"PROGRESS PROGRAM"]
    present = [item.decode() for item in forbidden if item in data]
    if present:
        raise SystemExit(f"PMOSLIVE retained flash-write markers: {present}")
    descriptor = {
        "format": "postmerkos.uart-liveboot-payload.v1",
        "protocol_version": 3,
        "soc_family": args.family,
        "soc_family_id": target["id"],
        "accepted_models": target["models"],
        "operations": ["verify", "dry-run", "liveboot"],
        "flash_access": "none",
        "load_address": 0x86C00000,
        "entry_address": 0x86C00000,
        "entry_contract": "flat-binary-byte-zero-v1",
        "transport_contract": "pmosrec-v3-adaptive-uart-sparse-lz4-v1",
        "image": {"bytes": 16 * 1024 * 1024, "kernel_offset": 0x00040000, "squashfs_offset": 0x00300000},
        "ram_layout": {
            "kernel_load_address": 0x81000000,
            "image_staging_address": 0x81400000,
            "manifest_address": 0x82400000,
            "payload_address": 0x86C00000,
            "squashfs_address": 0x87000000,
            "boot_params_physical_address": 0x00000400,
            "boot_params_uncached_address": 0xA0000400,
            "boot_params_bytes": 0x00000C00,
            "linux_memory_mib": 120,
            "top_reserved_mib": 8,
        },
        "linux_handoff": "mips-legacy-argc-argv-envp-external-initrd-v1",
        "platform_identity_handoff": "kernel-command-line-postmerkos-model-v1",
        "rootfs_handoff": "squashfs-as-legacy-initrd-v1",
        "binary": {"filename": args.binary.name, "bytes": len(data), "sha256": hashlib.sha256(data).hexdigest()},
    }
    args.output.write_text(json.dumps(descriptor, indent=2, sort_keys=True) + "\n")
    return 0

if __name__ == "__main__":
    raise SystemExit(main())
