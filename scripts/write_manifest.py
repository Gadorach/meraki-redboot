#!/usr/bin/env python3
from __future__ import annotations

import argparse
import hashlib
import json
import subprocess
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
    ap.add_argument("--uart-stage1-addr", type=integer, required=True)
    ap.add_argument("--uart-stage1-elf", type=Path)
    ap.add_argument("--uart-stage1-bin", type=Path)
    ap.add_argument("--compiler", required=True)
    ap.add_argument("--linker", required=True)
    ap.add_argument("--toolchain-id", required=True)
    ap.add_argument("--loader", type=Path, required=True)
    ap.add_argument("--image", type=Path, required=True)
    ap.add_argument("--output", type=Path, required=True)
    args = ap.parse_args()
    sources = sorted(Path("src").glob("*")) + sorted(Path("include").rglob("*.h"))
    uart_enabled = enabled(args.uart_ramloader)
    loader_data = args.loader.read_bytes()
    if uart_enabled and b"PMOSRAM READY 2" not in loader_data:
        raise SystemExit("UART RAM-loader was requested but the embedded stage1 marker is absent")
    if uart_enabled and (args.uart_stage1_elf is None or args.uart_stage1_bin is None):
        raise SystemExit("UART RAM-loader requires stage1 ELF and binary metadata")
    if args.uart_ram_start >= args.uart_ram_end:
        raise SystemExit("UART RAM range is invalid")
    compiler_version = subprocess.run(
        [args.compiler, "--version"], check=True, text=True, capture_output=True
    ).stdout.splitlines()[0]
    compiler_dumpversion = subprocess.run(
        [args.compiler, "-dumpversion"], check=True, text=True, capture_output=True
    ).stdout.strip()
    linker_version = subprocess.run(
        [args.linker, "--version"], check=True, text=True, capture_output=True
    ).stdout.splitlines()[0]
    data = {
        "format": "postmerkos.vcoreiii-linuxloader-build.v6",
        "toolchain": {
            "id": args.toolchain_id,
            "compiler": args.compiler,
            "compiler_version": compiler_version,
            "compiler_dumpversion": compiler_dumpversion,
            "linker": args.linker,
            "linker_version": linker_version,
            "code_generation_model": "stackless-flash-stage-plus-fixed-ram-boot-continuation",
        },
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
            "maximum_payload_bytes": args.uart_max_size,
            "ram_start": args.uart_ram_start,
            "ram_end": args.uart_ram_end,
            "supported_soc_families": ["luton26", "jaguar1"],
            "transport_integrity": ["frame-crc32", "object-crc32", "object-sha256"],
            "cache_maintenance": "mips32r2-dcache-writeback-invalidate-icache-invalidate",
            "execution_model": "embedded fixed-RAM stage owns UART recovery and flash-kernel boot continuation",
            "stage1_load_address": args.uart_stage1_addr if uart_enabled else None,
            "stage1_elf": ({
                "path": str(args.uart_stage1_elf),
                "size": args.uart_stage1_elf.stat().st_size,
                "sha256": sha(args.uart_stage1_elf),
            } if uart_enabled else None),
            "stage1_binary": ({
                "path": str(args.uart_stage1_bin),
                "size": args.uart_stage1_bin.stat().st_size,
                "sha256": sha(args.uart_stage1_bin),
            } if uart_enabled else None),
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
            "The flash-resident loader contains no ordinary C UART calls; fixed-RAM stage 1 owns UART recovery, SPIM kernel validation/copy, fallback, and kernel entry.",
        ],
    }
    args.output.write_text(json.dumps(data, indent=2, sort_keys=True) + "\n")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
