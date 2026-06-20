# UART RAM-loader protocol v2

`UART_RAMLOADER=1` adds a bounded pre-kernel serial upload window to the
source-built VCore-III LinuxLoader. The hook runs only after SoC initialization,
DDR training, cache setup, stack setup, SPI mapping, and boot-mode exit.

## Two-stage execution model

The flash-resident LinuxLoader is position-dependent at an unknown runtime
offset because the active and fallback copies can be placed at different
locations inside the 256 KiB wrapper. GCC 4.7.3 accepts the historical
`-fPIC -mno-abicalls` combination, but ordinary C calls and literal references
are not reliably relocatable under that model.

The UART implementation is therefore split into two parts:

1. **Flash stage:** a small assembly-only shim in `src/head.S` remains relative
   to the current loader's runtime `gp`. It prints a progress marker, copies an
   embedded binary to RAM, passes boot context, and transfers control with a
   non-returning `jr`.
2. **RAM stage 1:** `src/uart_ramloader.c` is linked normally at the fixed
   uncached KSEG1 address `0xa7f00000`. It owns UART upload, normal SPIM kernel
   validation/copy, cache maintenance, fallback selection, and kernel entry.

The upload interval remains the cached KSEG0 range
`[0x81000000, 0x87f00000)`. Stage 1 occupies the uncached alias of the physical
top 1 MiB beginning at `0xa7f00000`, so an uploaded program cannot overwrite
the receiver while it is running.

The original v0.5.x return-to-flash path was removed after hardware showed that
control returned far enough to print `PMOSRAM STAGE1 RETURN` but the legacy
flash kernel path did not proceed. Stage 1 now receives four register arguments:
SoC family, payload-header address, next fallback entry, and current loader base.
It never intentionally returns to the relocatable flash loader.

The build validates that stage 1:

- enters at exactly `0xa7f00000`;
- fits its reserved 1 MiB window;
- has no BSS requiring separate initialization;
- has no GOT, PLT, dynamic-loader state, unresolved symbols, or final
  relocations;
- keeps every direct MIPS `J/JAL` target inside its own linked address window.

## Expected serial sequence

A development boot should reach:

```text
Low level initialization complete, exiting boot mode
PMOSRAM STAGE1 COPY
PMOSRAM READY 2 SOC=jaguar1 MAX=... RAM=81000000-87f00000 ...
```

If no byte arrives during the default three-second probe interval, stage 1
continues directly into the flash-kernel loader:

```text
PMOSBOOT UART-DONE
PMOSBOOT FLASH HEADER=40040000
PMOSBOOT KERNEL LOAD=81000000 SIZE=... ENTRY=81000000
PMOSBOOT EXEC 81000000
```

A malformed or incomplete UART header prints `PMOSRAM ABORT ...` before the
same `PMOSBOOT` sequence. These markers localize hardware failures:

- no `STAGE1 COPY`: boot-mode exit/remap or flash-shim problem;
- `STAGE1 COPY` but no `READY`: stage copy, fixed-RAM address, or stage entry;
- `READY` but no `UART-DONE`: UART receiver/probe path;
- `PMOSBOOT FAIL ...`: explicit SPIM header, size, address, CRC, or fallback failure;
- `PMOSBOOT EXEC` but no kernel output: kernel entry/cache/handoff failure.

## Build

```sh
make all VARIANT=development
```

Relevant overrides:

```sh
make all \
  VARIANT=development \
  UART_RAMLOADER=1 \
  UART_RAMLOADER_MAX_SIZE=0x00400000 \
  UART_RAMLOADER_RAM_START=0x81000000 \
  UART_RAMLOADER_RAM_END=0x87f00000 \
  UART_RAMLOADER_STAGE1_ADDR=0xa7f00000 \
  UART_RAMLOADER_PROBE_TIMEOUT_MS=3000 \
  UART_RAMLOADER_INTERBYTE_TIMEOUT_MS=3000
```

The stage-1 address must be the uncached KSEG1 alias of
`UART_RAMLOADER_RAM_END`; the configuration checker rejects inconsistent
geometry.

Generated stage files are retained under:

```text
.work/build/development/uart-stage1/uart-stage1.elf
.work/build/development/uart-stage1/uart-stage1.bin
.work/build/development/uart-stage1/uart-stage1.elf.dis
.work/build/development/uart-stage1/uart-stage1.elf.readelf
.work/logs/uart-stage1-validation-development.log
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

Each payload frame contains:

```text
magic                 4 bytes: RMF2
sequence              u32
payload_bytes         u32
payload_crc32         u32
payload               payload_bytes
```

The target acknowledges each accepted sequence. A retransmission is accepted
only when its sequence, byte count, declared CRC-32, and computed CRC-32 match
the most recently accepted frame; it is then acknowledged without duplicating
data. Sequence gaps or CRC failures receive a NACK for the expected sequence.

After all bytes are received, the target verifies whole-object CRC-32 and
SHA-256, writes back and invalidates the data cache, invalidates the instruction
cache, and jumps to the validated entry point.

Receive time is bounded in three places:

- probe window before a header begins;
- total time for each header or frame block;
- 15-minute maximum for the complete RAM payload transfer.

No separate magic trigger is used. The first received byte is the first byte of
the `PMOSRAM2` header. A malformed header, idle timeout, transfer timeout,
digest mismatch, or unsupported address drains the UART receive FIFO and
returns to normal kernel boot.

## Host sender

For an MS42/MS42P Jaguar1 switch:

```sh
python3 tools/uart_ramload_send.py \
  --port /dev/ttyUSB0 \
  --binary .work/recovery/artifacts/recovery-jaguar1.bin \
  --load-address 0x81000000 \
  --entry 0x81000000 \
  --follow
```

The sender waits for `PMOSRAM READY 2`, displays the target-reported SoC family,
handles partial nonblocking writes, retries NACKed or timed-out
frames, and accepts the final `PMOSRAM VERIFIED` response as an implicit final
ACK if the explicit ACK was lost. It exits only after verification and
execution are reported.

Only one process should own the serial port during testing. Because any received
byte starts header reception, terminal-program setup bytes can intentionally or
accidentally enter the upload path.

## Safety properties

- Unknown header flags are rejected.
- The complete load interval is checked against the configured DRAM range.
- Stage 1 and uploaded payload memory ranges do not overlap.
- Header, frame, and whole-object integrity are independently verified.
- Interrupted receive states are timeout-bounded.
- Freshly received executable memory receives explicit D-cache and I-cache
  maintenance before control transfer.
- The uploaded program still executes with pre-kernel physical access. Use only
  on a bench with a verified complete SPI backup and an external programmer.

The firmware recovery payload layered on this protocol is documented in
[`payloads/uart-firmware-recovery/README.md`](../payloads/uart-firmware-recovery/README.md).
