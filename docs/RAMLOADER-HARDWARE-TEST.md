# Stage-2 menu hardware test

Use a board with a verified 16 MiB SPI backup and external programmer access.
Only one process may own the UART port.

## Normal no-input boot

Expected beginning:

```text
PMOSRAM STAGE1 COPY
PMOSBOOT CONTEXT LOADER=... PAYLOAD=40040000 FALLBACK=40400000
PMOSBOOT MENU-PROBE TIMEOUT_MS=00000bb8
PMOSBOOT PASS-MENU-PROBE: NO INPUT; CONTINUING NORMAL BOOT
```

The structured image checks then end with `PASS-EXEC` and Linux output.

## Noise timeout

Send a non-option byte during the three-second probe, then send nothing. The
menu must appear and, after five seconds, print `WARN-MENU-TIMEOUT` before
continuing normal boot.

## PMOSRAM option 1

Start before power-on:

```sh
python3 tools/uart_ramload_send.py \
  --port /dev/ttyUSB0 \
  --binary .work/recovery/artifacts/recovery-jaguar1.bin \
  --load-address 0x81000000 \
  --entry 0x81000000 \
  --follow
```

The tool triggers the menu, selects `1`, uploads, verifies CRC/SHA, and executes
the binary. `PMOSREC READY 3 SOC=jaguar1` proves the RAM-loader path works.

## Embedded recovery option 2

```sh
python3 tools/uart_boot_menu.py --port /dev/ttyUSB0 --choice 2 --follow
```

Expected markers include `PASS-RECOVERY-SIZE`, `PASS-RECOVERY-COPY`,
`PASS-RECOVERY-EXEC`, and `PMOSREC READY 3` for the detected family. Without a
firmware package the recovery program times out and halts without erasing flash.

## Automatic recovery

A fatal kernel check prints the exact `FAIL-*` comparison and then launches the
same embedded platform recovery. CRC warning mode continues after `WARN-CRC`;
strict CRC mode enters recovery after `FAIL-CRC`.
