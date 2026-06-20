#!/usr/bin/env python3
"""Upload a flat executable through LinuxLoader PMOSRAM protocol v2."""
from __future__ import annotations

import argparse
import hashlib
import os
import select
import struct
import sys
import termios
import time
import zlib

MAGIC = b"PMOSRAM2"
FRAME_MAGIC = b"RMF2"
HEADER = struct.Struct("<8s7I32sI")
FRAME = struct.Struct("<4sIII")


class ProtocolError(RuntimeError):
    pass


class SerialLink:
    def __init__(self, fd: int) -> None:
        self.fd = fd
        self.buffer = bytearray()

    def write_all(self, data: bytes, timeout: float = 10.0) -> None:
        view = memoryview(data)
        deadline = time.monotonic() + timeout
        while view:
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                raise ProtocolError("serial write timed out")
            _, writable, _ = select.select([], [self.fd], [], min(remaining, 0.25))
            if not writable:
                continue
            try:
                count = os.write(self.fd, view)
            except (BlockingIOError, InterruptedError):
                continue
            if count <= 0:
                raise ProtocolError("serial write made no progress")
            view = view[count:]

    def read_line(self, timeout: float) -> str:
        deadline = time.monotonic() + timeout
        while True:
            newline = self.buffer.find(b"\n")
            if newline >= 0:
                raw = bytes(self.buffer[: newline + 1])
                del self.buffer[: newline + 1]
                return raw.rstrip(b"\r\n").decode("utf-8", "replace")
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                raise ProtocolError("timed out waiting for target response")
            readable, _, _ = select.select([self.fd], [], [], min(remaining, 0.25))
            if not readable:
                continue
            try:
                data = os.read(self.fd, 4096)
            except (BlockingIOError, InterruptedError):
                continue
            if data:
                sys.stdout.buffer.write(data)
                sys.stdout.buffer.flush()
                self.buffer.extend(data)

    def wait_for(self, prefixes: tuple[str, ...], timeout: float) -> str:
        deadline = time.monotonic() + timeout
        while True:
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                raise ProtocolError(f"timed out waiting for {prefixes}")
            line = self.read_line(remaining)
            if line.startswith("PMOSRAM ABORT"):
                raise ProtocolError(line)
            if line.startswith(prefixes):
                return line


def baud_constant(baud: int) -> int:
    name = f"B{baud}"
    if not hasattr(termios, name):
        raise ProtocolError(f"unsupported baud rate: {baud}")
    return getattr(termios, name)


def configure(fd: int, baud: int) -> list:
    old = termios.tcgetattr(fd)
    attrs = termios.tcgetattr(fd)
    speed = baud_constant(baud)
    attrs[0] = 0
    attrs[1] = 0
    attrs[2] = speed | termios.CLOCAL | termios.CREAD | termios.CS8
    if hasattr(termios, "CRTSCTS"):
        attrs[2] &= ~termios.CRTSCTS
    attrs[3] = 0
    attrs[4] = speed
    attrs[5] = speed
    attrs[6][termios.VMIN] = 0
    attrs[6][termios.VTIME] = 1
    termios.tcsetattr(fd, termios.TCSANOW, attrs)
    termios.tcflush(fd, termios.TCIOFLUSH)
    return old


def make_header(payload: bytes, load: int, entry: int, chunk_size: int) -> bytes:
    if not payload or len(payload) > 4 * 1024 * 1024:
        raise ProtocolError("payload must be from 1 byte through 4 MiB")
    if not (0x81000000 <= load and load + len(payload) <= 0x87F00000):
        raise ProtocolError("payload does not fit the supported DRAM range")
    if (load | entry) & 3 or not (load <= entry < load + len(payload)):
        raise ProtocolError("load/entry addresses are unaligned or entry is outside the payload")
    if not 64 <= chunk_size <= 4096:
        raise ProtocolError("chunk size must be from 64 through 4096")
    base = HEADER.pack(
        MAGIC, 2, 0, load, entry, len(payload), chunk_size,
        zlib.crc32(payload) & 0xFFFFFFFF, hashlib.sha256(payload).digest(), 0,
    )
    return base[:-4] + struct.pack("<I", zlib.crc32(base[:-4]) & 0xFFFFFFFF)


def send_frame(
    link: SerialLink, frame: bytes, sequence: int, retries: int, timeout: float,
    completion_prefix: str | None = None,
) -> str:
    ack = f"PMOSRAM ACK {sequence:08x}"
    nack = f"PMOSRAM NACK {sequence:08x}"
    prefixes = (ack, nack) + ((completion_prefix,) if completion_prefix else ())
    for attempt in range(retries + 1):
        link.write_all(frame)
        try:
            line = link.wait_for(prefixes, timeout)
        except ProtocolError as exc:
            if "timed out" not in str(exc) or attempt == retries:
                raise
            continue
        if line.startswith(ack) or (completion_prefix and line.startswith(completion_prefix)):
            return line
        if attempt == retries:
            raise ProtocolError(f"target rejected frame {sequence}")
    raise ProtocolError(f"frame {sequence} retry limit exhausted")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", required=True)
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--binary", required=True)
    parser.add_argument("--load-address", default="0x81000000")
    parser.add_argument("--entry")
    parser.add_argument("--chunk-size", type=int, default=1024)
    parser.add_argument("--ready-timeout", type=float, default=30.0)
    parser.add_argument("--ack-timeout", type=float, default=5.0)
    parser.add_argument("--retries", type=int, default=3)
    parser.add_argument("--follow", action="store_true", help="continue displaying serial output after execution starts")
    args = parser.parse_args()

    payload = open(args.binary, "rb").read()
    load = int(args.load_address, 0)
    entry = int(args.entry, 0) if args.entry else load
    header = make_header(payload, load, entry, args.chunk_size)

    fd = os.open(args.port, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
    old = configure(fd, args.baud)
    link = SerialLink(fd)
    try:
        print("Reset or power-cycle the target; waiting for the stage-1 menu probe...")
        line = link.wait_for(("PMOSMENU PROBE", "PMOSRAM READY 2"), args.ready_timeout)
        if line.startswith("PMOSMENU PROBE"):
            # The first byte only interrupts the boot window.  A separate
            # explicit menu selection prevents random UART noise from choosing
            # a destructive or blocking path.
            link.write_all(b"\n")
            link.wait_for(("PMOSMENU SELECT",), 5.0)
            link.write_all(b"1\n")
            link.wait_for(("PMOSRAM READY 2",), 5.0)
        link.write_all(header)
        link.wait_for(("PMOSRAM HEADER-ACK",), 5.0)
        verified: str | None = None
        for sequence, offset in enumerate(range(0, len(payload), args.chunk_size)):
            data = payload[offset : offset + args.chunk_size]
            frame = FRAME.pack(FRAME_MAGIC, sequence, len(data), zlib.crc32(data) & 0xFFFFFFFF) + data
            final_frame = offset + len(data) == len(payload)
            response = send_frame(
                link, frame, sequence, args.retries, args.ack_timeout,
                "PMOSRAM VERIFIED" if final_frame else None,
            )
            if response.startswith("PMOSRAM VERIFIED"):
                verified = response
            sent = offset + len(data)
            percent = int(sent * 100 / len(payload))
            if percent == 100 or percent % 5 == 0:
                print(f"sent {sent}/{len(payload)} bytes ({percent}%)")
        if verified is None:
            verified = link.wait_for(("PMOSRAM VERIFIED",), 10.0)
        exec_line = link.wait_for(("PMOSRAM EXEC",), 5.0)
        print(exec_line)
        if args.follow:
            print("payload is running; press Ctrl-C to exit")
            while True:
                link.read_line(3600.0)
        return 0
    except KeyboardInterrupt:
        return 0
    finally:
        termios.tcsetattr(fd, termios.TCSANOW, old)
        os.close(fd)


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except ProtocolError as exc:
        print(f"UART RAM-loader error: {exc}", file=sys.stderr)
        raise SystemExit(1)
