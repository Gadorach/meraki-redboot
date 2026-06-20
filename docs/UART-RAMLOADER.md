# Stage-2 UART menu and PMOSRAM protocol v2

A UART-enabled loader copies a fixed-address stage to `0xa7f00000`. The stage
contains the PMOSRAM executable uploader plus the Luton26 and Jaguar1 firmware
recovery programs.

## Boot menu

Normal boot prints:

```text
PMOSBOOT MENU-PROBE TIMEOUT_MS=00000bb8
```

No input for three seconds continues flash-kernel validation. Any received byte
opens the menu, but that trigger byte is discarded:

```text
PMOSBOOT MENU 1=UART-RAMLOADER 2=FW-RECOVERY
PMOSBOOT MENU-READY TIMEOUT_MS=00001388
```

A fresh explicit `1` or `2` is required within five seconds. Invalid characters
are reported as `WARN-MENU-INPUT`; timeout reports `WARN-MENU-TIMEOUT` and
continues normal boot. This prevents incidental UART noise from permanently
halting startup.

- `1`: persistent PMOSRAM executable uploader; power-cycle to exit.
- `2`: copy the embedded recovery matching the detected SoC to `0x81000000`
  and execute it.

Fatal flash-kernel checks also launch the matching embedded recovery directly.
The old next-flash-region fallback address is retained only in context output.

## PMOSRAM wire contract

The little-endian 72-byte header contains `PMOSRAM2`, protocol version 2, flags,
load/entry addresses, object size, chunk size, whole-object CRC-32 and SHA-256,
and header CRC-32. Each frame contains `RMF2`, sequence, length, CRC-32, and
payload bytes. Accepted frames receive ACK; sequence/CRC errors receive NACK.

All header, address, size, CRC, SHA, and execution checks emit structured
`PASS-*`, `WARN-*`, `FAIL-*`, or `SKIP-*` records with compared values.

## Host tools

The normal uploader automatically performs the menu handshake and selects
option 1:

```sh
python3 tools/uart_ramload_send.py \
  --port /dev/ttyUSB0 \
  --binary diagnostic.bin \
  --load-address 0x81000000 \
  --entry 0x81000000 \
  --follow
```

Use the menu-only helper to test either option:

```sh
python3 tools/uart_boot_menu.py --port /dev/ttyUSB0 --choice 1 --follow
python3 tools/uart_boot_menu.py --port /dev/ttyUSB0 --choice 2 --follow
```

Only one process may own the serial port. The recovery payload executes with
pre-kernel physical access; destructive flashing still requires its package,
manifest validation, and explicit erase challenge response.
