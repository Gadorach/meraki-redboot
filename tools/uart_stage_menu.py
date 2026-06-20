#!/usr/bin/env python3
"""Select a fixed-RAM stage-1 boot menu option over UART."""
from __future__ import annotations

import argparse
import os
import sys
import termios

from uart_ramload_send import ProtocolError, SerialLink, configure


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--port", required=True)
    ap.add_argument("--baud", type=int, default=115200)
    ap.add_argument("--option", choices=("1", "2"), required=True,
                    help="1 = RAM loader, 2 = platform firmware recovery")
    ap.add_argument("--ready-timeout", type=float, default=30.0)
    ap.add_argument("--follow", action="store_true")
    args = ap.parse_args()

    fd = os.open(args.port, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
    old = configure(fd, args.baud)
    link = SerialLink(fd)
    try:
        print("Reset or power-cycle the target; waiting for PMOSMENU PROBE...")
        link.wait_for(("PMOSMENU PROBE",), args.ready_timeout)
        link.write_all(b"\n")
        link.wait_for(("PMOSMENU SELECT",), 5.0)
        link.write_all(args.option.encode("ascii") + b"\n")
        expected = "PMOSRAM READY 2" if args.option == "1" else "PMOSREC CHAINLOAD"
        line = link.wait_for((expected,), 5.0)
        print(line)
        if args.follow:
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
        print(f"stage-1 menu error: {exc}", file=sys.stderr)
        raise SystemExit(1)
