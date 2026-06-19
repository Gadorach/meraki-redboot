#!/usr/bin/env python3
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path


def sha(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def integer(value: str) -> int:
    return int(value, 0)


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--variant", required=True)
    ap.add_argument("--crc-policy", choices=("strict", "warn", "off"), required=True)
    ap.add_argument(
        "--size-policy", choices=("legacy-strict", "legacy-warn", "hard-only"), required=True
    )
    ap.add_argument("--fallback-region-size", type=integer, required=True)
    ap.add_argument("--payload-slot-end", type=integer, required=True)
    ap.add_argument("--legacy-payload-limit", type=integer, required=True)
    ap.add_argument("--hard-payload-limit", type=integer, required=True)
    ap.add_argument("--loader", type=Path, required=True)
    ap.add_argument("--image", type=Path, required=True)
    ap.add_argument("--output", type=Path, required=True)
    args = ap.parse_args()
    sources = sorted(Path("src").glob("*")) + sorted(Path("include").rglob("*.h"))
    data = {
        "format": "postmerkos.vcoreiii-linuxloader-build.v2",
        "variant": args.variant,
        "policies": {
            "crc": args.crc_policy,
            "size": args.size_policy,
            "fallback_region_size": args.fallback_region_size,
            "payload_slot_end": args.payload_slot_end,
            "legacy_payload_limit": args.legacy_payload_limit,
            "hard_payload_limit": args.hard_payload_limit,
        },
        "loader": {
            "path": str(args.loader),
            "size": args.loader.stat().st_size,
            "sha256": sha(args.loader),
        },
        "boot_region": {
            "path": str(args.image),
            "size": args.image.stat().st_size,
            "sha256": sha(args.image),
        },
        "source_sha256": {str(p): sha(p) for p in sources if p.is_file()},
        "notes": [
            "Built entirely from source; GPL-adjacent RedBoot binaries are not build inputs.",
            "The 256 KiB wrapper contains active and fallback copies of the same compiled loader.",
            "The hard payload boundary is always enforced, independent of selected compatibility policy.",
        ],
    }
    args.output.write_text(json.dumps(data, indent=2, sort_keys=True) + "\n")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
