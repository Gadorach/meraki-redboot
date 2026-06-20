# PMOSRAM UART execution smoke test

This fixed-address payload is the safest first test of the PMOSRAM upload and
execution path. It is linked at `0x81000000`, prints:

```text
PMOSRAM SMOKE OK
```

and returns to fixed-RAM stage 1. Stage 1 should then print `PMOSRAM RETURNED`,
validate/copy the normal flash kernel, print `PMOSRAM FLASH-BOOT ...`, and boot
it. No flash write is performed.

Build from the repository root:

```sh
make uart-smoke-test
```

Upload while resetting or power-cycling the switch:

```sh
python3 tools/uart_ramload_send.py \
  --port /dev/ttyUSB0 \
  --binary payloads/uart-smoke-test/artifacts/uart-smoke-test.bin \
  --load-address 0x81000000 \
  --entry 0x81000000 \
  --follow
```

Expected sequence after the transfer:

```text
PMOSRAM VERIFIED SHA256=...
PMOSRAM EXEC 81000000
PMOSRAM SMOKE OK
PMOSRAM RETURNED
PMOSRAM FLASH-BOOT LOAD=... SIZE=... ENTRY=...
```
