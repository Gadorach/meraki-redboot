# UART RAM-loader hardware test

Use an MS42/MS42P with a verified full SPI backup and external programmer
available. Only one process may own the UART port.

## 1. Normal no-input boot

Flash only the 256 KiB development boot region and power-cycle without sending
serial input. The expected continuation is:

```text
Low level initialization complete, exiting boot mode
PMOSRAM STAGE1 COPY
PMOSBOOT CONTEXT LOADER=... PAYLOAD=40040000 FALLBACK=40400000
PMOSRAM READY 2 SOC=jaguar1 ...
PMOSBOOT UART-DONE
PMOSBOOT FLASH HEADER=40040000
PMOSBOOT KERNEL LOAD=81000000 SIZE=... ENTRY=81000000
PMOSBOOT EXEC 81000000
```

Linux kernel output should follow `PMOSBOOT EXEC`. A `PMOSBOOT FAIL` line gives
the exact reason and fallback address.

## 2. Upload and execute the Jaguar1 recovery payload

Close `screen`, `minicom`, and every other program using the serial port. From
the repository root, start the sender before powering the switch:

```sh
python3 tools/uart_ramload_send.py \
  --port /dev/ttyUSB0 \
  --baud 115200 \
  --binary .work/recovery/artifacts/recovery-jaguar1.bin \
  --load-address 0x81000000 \
  --entry 0x81000000 \
  --chunk-size 1024 \
  --ready-timeout 60 \
  --follow
```

Use `sudo` only when the account lacks access to the serial device. Power-cycle
the switch after the sender reports that it is waiting.

A successful RAM-loader transfer produces:

```text
PMOSRAM HEADER-ACK SIZE=... LOAD=81000000 ENTRY=81000000
PMOSRAM ACK 00000000
...
PMOSRAM VERIFIED SHA256=...
PMOSRAM EXEC 81000000
PMOSREC READY 2 SOC=jaguar1 FAMILY=00000002 SPI=70000068 ...
PMOSREC DESCRIPTOR PMOSRECOVERY2;SOC=jaguar1;...
```

The two `PMOSREC` lines prove that the executable was received, verified,
cache-maintained, and entered successfully. Without a `PMOSPKG2` firmware
package, the recovery payload eventually prints
`PMOSREC RESULT ERROR PACKAGE-HEADER-TIMEOUT` and halts. Power-cycle afterward.
No erase or program operation is possible at this stage because no package or
erase confirmation was supplied.

## 3. Failure localization

- No `PMOSRAM READY 2`: fixed-RAM copy or stage entry failure.
- No `HEADER-ACK`: serial header was not received or rejected.
- Repeated `NACK`: frame sequence or CRC mismatch.
- `VERIFIED` but no `EXEC`: cache/jump path failure.
- `EXEC` but no `PMOSREC READY`: uploaded payload entry or address mismatch.
- `PMOSBOOT FAIL MAGIC`: the flash at `0x40040000` does not contain the expected
  SPIM header.
- `PMOSBOOT EXEC` but no Linux output: final kernel cache/entry handoff failure.
