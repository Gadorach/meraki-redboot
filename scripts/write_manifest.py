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


def enabled(value: str) -> bool:
    return value.lower() in {"1", "y", "yes", "true", "on", "enabled"}


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
    ap.add_argument("--uart-ramloader", required=True)
    ap.add_argument("--uart-protocol-version", type=integer, required=True)
    ap.add_argument("--uart-max-size", type=integer, required=True)
    ap.add_argument("--uart-ram-start", type=integer, required=True)
    ap.add_argument("--uart-ram-end", type=integer, required=True)
    ap.add_argument("--uart-probe-timeout-ms", type=integer, required=True)
    ap.add_argument("--uart-interbyte-timeout-ms", type=integer, required=True)
    ap.add_argument("--uart-menu-timeout-ms", type=integer, required=True)
    ap.add_argument("--loader", type=Path, required=True)
    ap.add_argument("--image", type=Path, required=True)
    ap.add_argument("--toolchain-info", type=Path, required=True)
    ap.add_argument("--codegen-report", type=Path, required=True)
    ap.add_argument("--uart-stage1", type=Path)
    ap.add_argument("--uart-stage1-address", type=integer)
    ap.add_argument("--uart-stage1-report", type=Path)
    ap.add_argument("--recovery-luton26", type=Path)
    ap.add_argument("--recovery-jaguar1", type=Path)
    ap.add_argument("--output", type=Path, required=True)
    args = ap.parse_args()
    sources = sorted(Path("src").glob("*")) + sorted(Path("include").rglob("*.h"))
    uart_enabled = enabled(args.uart_ramloader)
    loader_data = args.loader.read_bytes()
    stage1_data = args.uart_stage1.read_bytes() if args.uart_stage1 else b""
    if uart_enabled and b"PMOSRAM READY 2" not in stage1_data:
        raise SystemExit("UART RAM-loader was requested but fixed-RAM stage 1 lacks its v2 marker")
    if uart_enabled and b"PMOSMENU SELECT" not in stage1_data:
        raise SystemExit("UART stage 1 lacks the explicit recovery menu marker")
    if uart_enabled and b"PMOSREC CHAINLOAD" not in stage1_data:
        raise SystemExit("UART stage 1 lacks embedded recovery chainload support")
    if uart_enabled and not args.uart_stage1:
        raise SystemExit("UART RAM-loader was requested without a stage-1 binary")
    if uart_enabled and (not args.recovery_luton26 or not args.recovery_jaguar1):
        raise SystemExit("UART stage 1 requires both embedded recovery binaries")
    if args.uart_ram_start >= args.uart_ram_end:
        raise SystemExit("UART RAM range is invalid")
    data = {
        "format": "postmerkos.vcoreiii-linuxloader-build.v3",
        "variant": args.variant,
        "policies": {
            "crc": args.crc_policy,
            "size": args.size_policy,
            "fallback_region_size": args.fallback_region_size,
            "payload_slot_end": args.payload_slot_end,
            "legacy_payload_limit": args.legacy_payload_limit,
            "hard_payload_limit": args.hard_payload_limit,
        },
        "uart_ramloader": {
            "enabled": uart_enabled,
            "protocol_version": args.uart_protocol_version if uart_enabled else None,
            "probe_timeout_ms": args.uart_probe_timeout_ms,
            "interbyte_timeout_ms": args.uart_interbyte_timeout_ms,
            "menu_timeout_ms": args.uart_menu_timeout_ms,
            "menu_options": {"1": "ram-loader", "2": "firmware-recovery"},
            "maximum_payload_bytes": args.uart_max_size,
            "ram_start": args.uart_ram_start,
            "ram_end": args.uart_ram_end,
            "supported_soc_families": ["luton26", "jaguar1"],
            "transport_integrity": ["frame-crc32", "object-crc32", "object-sha256"],
            "cache_maintenance": "mips32r2-dcache-writeback-invalidate-icache-invalidate",
            "execution_model": "embedded-fixed-ram-stage1",
            "stage1_address": args.uart_stage1_address if uart_enabled else None,
            "stage1_size": args.uart_stage1.stat().st_size if uart_enabled else None,
            "stage1_sha256": sha(args.uart_stage1) if uart_enabled else None,
            "stage1_codegen_validation": (
                args.uart_stage1_report.read_text().splitlines()
                if uart_enabled and args.uart_stage1_report else []
            ),
            "embedded_recovery": {
                "section_executable": False,
                "luton26": {
                    "size": args.recovery_luton26.stat().st_size if args.recovery_luton26 else None,
                    "sha256": sha(args.recovery_luton26) if args.recovery_luton26 else None,
                    "load_address": 0x81000000,
                    "entry_address": 0x81000000,
                },
                "jaguar1": {
                    "size": args.recovery_jaguar1.stat().st_size if args.recovery_jaguar1 else None,
                    "sha256": sha(args.recovery_jaguar1) if args.recovery_jaguar1 else None,
                    "load_address": 0x81000000,
                    "entry_address": 0x81000000,
                },
            },
        },
        "toolchain": {
            "details": args.toolchain_info.read_text().splitlines(),
            "codegen_validation": args.codegen_report.read_text().splitlines(),
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
            "The hard payload boundary is enforced independently of the selected compatibility policy.",
        ],
    }
    args.output.write_text(json.dumps(data, indent=2, sort_keys=True) + "\n")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
