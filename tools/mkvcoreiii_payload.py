#!/usr/bin/env python3
"""Create or verify the 32-byte VCore-III LinuxLoader payload header."""
from __future__ import annotations

import argparse
import json
import struct
import sys
import zlib
from pathlib import Path

MAGIC = 0x4D495053
HEADER = struct.Struct("<8I")
HEADER_SIZE = HEADER.size
DEFAULT_ALIGNMENT = 32
DEFAULT_MAX_PAYLOAD = 0x003BFFE0


def integer(value: str) -> int:
    try:
        result = int(value, 0)
    except ValueError as exc:
        raise argparse.ArgumentTypeError(str(exc)) from exc
    if not 0 <= result <= 0xFFFFFFFF:
        raise argparse.ArgumentTypeError("value must fit in an unsigned 32-bit word")
    return result


def crc_for(header_words: tuple[int, ...], payload: bytes) -> int:
    zero_crc = list(header_words)
    zero_crc[4] = 0
    return zlib.crc32(HEADER.pack(*zero_crc) + payload) & 0xFFFFFFFF


def unpack_image(data: bytes) -> tuple[tuple[int, ...], bytes]:
    if len(data) < HEADER_SIZE:
        raise ValueError("image is shorter than the 32-byte header")
    words = HEADER.unpack_from(data)
    size = words[2]
    if len(data) != HEADER_SIZE + size:
        raise ValueError(
            f"file length is {len(data)} bytes but header declares {size} payload bytes "
            f"({HEADER_SIZE + size} total)"
        )
    return words, data[HEADER_SIZE:]


def command_pack(args: argparse.Namespace) -> int:
    raw = args.input.read_bytes()
    if not raw:
        raise SystemExit("input kernel is empty")
    if args.alignment <= 0 or args.alignment & (args.alignment - 1):
        raise SystemExit("alignment must be a positive power of two")
    padding = (-len(raw)) % args.alignment
    payload = raw + bytes([args.pad_byte]) * padding
    if len(payload) > args.max_payload_size:
        raise SystemExit(
            f"padded payload is 0x{len(payload):x} bytes, exceeding maximum 0x{args.max_payload_size:x}"
        )

    base_words = (
        args.magic,
        args.load_address,
        len(payload),
        args.entry_point,
        0,
        args.reserved0,
        args.reserved1,
        args.reserved2,
    )
    crc = crc_for(base_words, payload)
    final_words = base_words[:4] + (crc,) + base_words[5:]
    image = HEADER.pack(*final_words) + payload
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes(image)

    metadata = {
        "format": "postmerkos.vcoreiii-payload.v1",
        "input": str(args.input),
        "output": str(args.output),
        "magic": f"0x{args.magic:08x}",
        "load_address": f"0x{args.load_address:08x}",
        "entry_point": f"0x{args.entry_point:08x}",
        "original_size": len(raw),
        "padding": padding,
        "payload_size": len(payload),
        "crc32": f"0x{crc:08x}",
        "reserved": [f"0x{x:08x}" for x in final_words[5:]],
    }
    if args.metadata:
        args.metadata.parent.mkdir(parents=True, exist_ok=True)
        args.metadata.write_text(json.dumps(metadata, indent=2, sort_keys=True) + "\n")

    print(
        f"packed {args.input} -> {args.output}: payload=0x{len(payload):x}, "
        f"padding={padding}, crc32=0x{crc:08x}"
    )
    return 0


def command_verify(args: argparse.Namespace) -> int:
    try:
        words, payload = unpack_image(args.image.read_bytes())
    except ValueError as exc:
        raise SystemExit(f"invalid payload image: {exc}") from exc
    magic, load, size, entry, stored_crc, r0, r1, r2 = words
    calculated = crc_for(words, payload)
    errors: list[str] = []
    if magic != args.expected_magic:
        errors.append(f"magic 0x{magic:08x} != expected 0x{args.expected_magic:08x}")
    if size == 0:
        errors.append("payload size is zero")
    if size % args.alignment:
        errors.append(f"payload size 0x{size:x} is not {args.alignment}-byte aligned")
    if size > args.max_payload_size:
        errors.append(f"payload size 0x{size:x} exceeds limit 0x{args.max_payload_size:x}")
    if stored_crc != calculated:
        errors.append(f"stored CRC 0x{stored_crc:08x} != calculated 0x{calculated:08x}")

    print(f"magic:        0x{magic:08x}")
    print(f"load address: 0x{load:08x}")
    print(f"payload size: 0x{size:x} ({size} bytes)")
    print(f"entry point:  0x{entry:08x}")
    print(f"stored CRC:   0x{stored_crc:08x}")
    print(f"computed CRC: 0x{calculated:08x}")
    print(f"reserved:     0x{r0:08x} 0x{r1:08x} 0x{r2:08x}")
    if errors:
        for error in errors:
            print(f"ERROR: {error}", file=sys.stderr)
        return 1
    print("payload header and CRC are valid")
    return 0


def parser() -> argparse.ArgumentParser:
    ap = argparse.ArgumentParser(description=__doc__)
    sub = ap.add_subparsers(dest="command", required=True)

    pack = sub.add_parser("pack", help="pad a kernel, add its header, and calculate CRC")
    pack.add_argument("--input", type=Path, required=True)
    pack.add_argument("--output", type=Path, required=True)
    pack.add_argument("--load-address", type=integer, required=True)
    pack.add_argument("--entry-point", type=integer, required=True)
    pack.add_argument("--magic", type=integer, default=MAGIC)
    pack.add_argument("--alignment", type=integer, default=DEFAULT_ALIGNMENT)
    pack.add_argument("--pad-byte", type=integer, default=0, choices=range(256), metavar="0..255")
    pack.add_argument("--max-payload-size", type=integer, default=DEFAULT_MAX_PAYLOAD)
    pack.add_argument("--reserved0", type=integer, default=0)
    pack.add_argument("--reserved1", type=integer, default=0)
    pack.add_argument("--reserved2", type=integer, default=0)
    pack.add_argument("--metadata", type=Path)
    pack.set_defaults(func=command_pack)

    verify = sub.add_parser("verify", help="validate an existing header, geometry, and CRC")
    verify.add_argument("image", type=Path)
    verify.add_argument("--expected-magic", type=integer, default=MAGIC)
    verify.add_argument("--alignment", type=integer, default=DEFAULT_ALIGNMENT)
    verify.add_argument("--max-payload-size", type=integer, default=DEFAULT_MAX_PAYLOAD)
    verify.set_defaults(func=command_verify)
    return ap


def main() -> int:
    args = parser().parse_args()
    return args.func(args)


if __name__ == "__main__":
    raise SystemExit(main())
