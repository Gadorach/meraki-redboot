# UART RAM-loader protocol v2

`UART_RAMLOADER=1` adds a fixed-RAM boot menu after SoC and DRAM initialization.
The complete stage is linked at `0xa7f00000`, embedded in both relocatable
loader copies, byte-copied to the uncached top-of-RAM alias, and entered with a
one-way tail jump.

Stage 1 is the permanent post-DDR continuation. It does not return to flash.
It owns UART selection, chunked RAM uploads, target-specific firmware-recovery
chainloading, normal SPIM kernel validation/copy/jump, and fatal flash recovery.

## Boot menu

A quiet boot waits three seconds and then continues to the flash kernel. Any
received byte is consumed solely as an interrupt trigger. Stage 1 then prints:

```text
PMOSMENU SELECT 1=RAM-LOADER 2=FW-RECOVERY TIMEOUT_MS=00001388
```

A separate explicit `1` or `2` must arrive within five seconds. Before printing
the selection prompt, stage 1 drains bytes already buffered behind the interrupt
character; that traffic cannot accidentally become a choice. CR/LF and other
new bytes are ignored. Menu timeout continues flash boot, preventing line noise
from holding the switch indefinitely.

- `1` starts `PMOSRAM2` and waits for a framed executable upload.
- `2` selects the recovery image matching the detected SoC, copies it to
  `0x81000000`, performs cache maintenance, and enters it.

Fatal flash checks do not chain to a later 4 MiB flash region. They enter the
same menu persistently and can only leave by executing uploaded code, entering
firmware recovery, successfully booting a valid kernel, or resetting the switch.
The active/fallback copies in the 256 KiB wrapper remain byte-identical.

## Build

```sh
make VARIANT=development \
  UART_RAMLOADER=1 \
  UART_RAMLOADER_MAX_SIZE=0x00400000 \
  UART_RAMLOADER_PROBE_TIMEOUT_MS=3000 \
  UART_MENU_TIMEOUT_MS=5000 \
  UART_RAMLOADER_INTERBYTE_TIMEOUT_MS=3000 \
  UART_STAGE1_ADDRESS=0xa7f00000
```

The fixed stage reservation is `[0xa7f00000, 0xa8000000)`, the uncached KSEG1
alias of the physical region beginning where the cached upload interval ends.
Uploaded programs use `[0x81000000, 0x87f00000)` by default. Both firmware
recovery images are linked at `0x81000000` and are embedded in a non-executable
`.embedded_recovery` section. Only `.text` is instruction-disassembled during
validation.

Outputs include:

```text
build/development/uart-stage1.elf
build/development/uart-stage1.bin
artifacts/vcoreiii-uart-stage1-development.bin
artifacts/vcoreiii-recovery-luton26.bin
artifacts/vcoreiii-recovery-jaguar1.bin
```

## Wire contract

The little-endian 72-byte header contains:

```text
magic                 8 bytes: PMOSRAM2
protocol_version      u32: 2
flags                 u32: must be zero
load_address          u32
entry_address         u32
object_bytes          u32
chunk_bytes           u32: 64 through 4096
object_crc32          u32: IEEE CRC-32
object_sha256         32 bytes
header_crc32          u32: CRC-32 over the preceding 68 bytes
```

Each frame contains `RMF2`, sequence, byte count, payload CRC-32, and payload.
The target ACKs each accepted sequence and tolerates an exact retransmission of
the most recently accepted frame. It verifies whole-object CRC-32 and SHA-256,
performs D-cache writeback/invalidation and I-cache invalidation, then calls the
validated entry point.

Receive time is bounded by the probe window, each requested header/frame block,
and a 15-minute complete-transfer deadline.

## First hardware execution test

Build the non-destructive smoke payload:

```sh
make uart-smoke-test
```

Start the sender, then reset the target. The sender waits for the menu probe,
sends a neutral interrupt byte, explicitly selects option 1, and then begins
the protocol:

```sh
python3 tools/uart_ramload_send.py \
  --port /dev/ttyUSB0 \
  --binary payloads/uart-smoke-test/artifacts/uart-smoke-test.bin \
  --load-address 0x81000000 \
  --entry 0x81000000 \
  --follow
```

Expected completion sequence:

```text
PMOSRAM VERIFIED SHA256=...
PMOSRAM EXEC 81000000
PMOSRAM SMOKE OK
PMOSRAM RETURNED
PMOSBOOT UART-DONE
PMOSBOOT FLASH HEADER=...
PMOSRAM FLASH-BOOT LOAD=... SIZE=... ENTRY=...
```

This proves framed upload, RAM reconstruction, digest verification, cache
maintenance, executable entry, return to stage 1, and the fixed-RAM flash-kernel
continuation without writing SPI flash.

## Host sender

`tools/uart_ramload_send.py` performs the two-step menu handshake, handles
partial nonblocking writes, retries NACKed or timed-out frames, verifies final
target status, and optionally follows subsequent output. For manual menu
selection use `tools/uart_stage_menu.py --option 1|2`.

## Safety properties

- Unknown flags, bad header CRC, invalid ranges, and invalid entry points are
  rejected.
- Header, frame, and whole-object integrity are independent.
- Every receive state is timeout-bounded.
- Stage 1 and uploaded executables receive explicit cache maintenance.
- Physical-alias overlap checks prevent cached kernel/upload addresses from
  overwriting uncached stage 1; the upload window also cannot reach the early
  stack.
- The normal flash kernel receives the same header, threshold, CRC, and cache
  policies as the prior assembly continuation. Stage 1 improves compatibility
  by copying the exact declared size instead of requiring the historical
  32-byte assembly-loop alignment.
- Fatal flash validation enters persistent UART recovery rather than trusting a
  later flash region.
- Embedded recovery bytes are in a non-executable ELF section and are compared
  byte-for-byte against their separately validated build artifacts.
- Uploaded code has pre-kernel physical access; use only on a bench with a
  verified SPI backup and external programmer.

Destructive firmware recovery is documented in
[`payloads/uart-firmware-recovery/README.md`](../payloads/uart-firmware-recovery/README.md).
