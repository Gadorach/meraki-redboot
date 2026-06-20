#!/usr/bin/env python3
"""Trigger the LinuxLoader stage-2 menu and explicitly select option 1 or 2."""
from __future__ import annotations

import argparse
import os
import termios

from uart_ramload_send import ProtocolError, SerialLink, configure


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", required=True)
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--choice", choices=("1", "2"), required=True)
    parser.add_argument("--ready-timeout", type=float, default=60.0)
    parser.add_argument("--follow", action="store_true")
    args = parser.parse_args()

    fd = os.open(args.port, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
    old = configure(fd, args.baud)
    link = SerialLink(fd)
    try:
        print("Reset or power-cycle the target; waiting for PMOSBOOT MENU-PROBE...")
        link.wait_for(("PMOSBOOT MENU-PROBE",), args.ready_timeout)
        link.write_all(b" ")
        link.wait_for(("PMOSBOOT MENU-READY",), 4.0)
        link.write_all(args.choice.encode("ascii"))
        expected = "PMOSRAM READY 2" if args.choice == "1" else "PMOSREC READY 2"
        line = link.wait_for((expected,), 10.0)
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
        print(f"UART boot-menu error: {exc}", file=__import__("sys").stderr)
        raise SystemExit(1)
