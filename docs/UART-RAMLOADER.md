# UART RAM-loader protocol v2

`UART_RAMLOADER=1` adds a bounded pre-kernel serial upload window to the
source-built VCore-III LinuxLoader. The hook runs after SoC and DRAM
initialization and before the normal SPIM kernel header is processed.

The loader reports its detected SoC family and waits for a protocol-v2 header
for the configured probe interval. No separate trigger byte is used. The first
byte received is the first byte of the `PMOSRAM2` header. A malformed header,
idle frame timeout, overall transfer timeout, digest mismatch, or unsupported
address returns control to the normal kernel boot path after the UART receive
FIFO is drained.

## Build

```sh
make VARIANT=development \
  UART_RAMLOADER=1 \
  UART_RAMLOADER_MAX_SIZE=0x00400000 \
  UART_RAMLOADER_PROBE_TIMEOUT_MS=3000 \
  UART_RAMLOADER_INTERBYTE_TIMEOUT_MS=3000
```

The default staging range is the half-open interval
`[0x81000000, 0x87f00000)`. The entire uploaded object and its entry point must
fit in that range. The default maximum object size is 4 MiB.

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
After all bytes are
received, the target verifies whole-object CRC-32 and SHA-256, writes back and
invalidates the data cache, invalidates the instruction cache, and jumps to the
validated entry point.

Receive time is bounded in three places:

- probe window before a header begins;
- total time for each header or frame block;
- 15-minute maximum for the complete RAM payload transfer.

## Host sender

```sh
python3 tools/uart_ramload_send.py \
  --port /dev/ttyUSB0 \
  --binary .work/recovery/artifacts/recovery-luton26.bin \
  --load-address 0x81000000 \
  --entry 0x81000000
```

The sender waits for `PMOSRAM READY 2`, validates the reported SoC family when
requested, handles partial nonblocking writes, retries NACKed or timed-out
frames, and accepts the final `PMOSRAM VERIFIED` response as an implicit final
ACK if the explicit ACK was lost. It exits only after verification and
execution are reported.

## Safety properties

- Unknown header flags are rejected.
- The complete load interval is checked against the configured DRAM range.
- Header, frame, and whole-object integrity are independently verified.
- Interrupted receive states are timeout-bounded.
- Freshly received executable memory receives explicit D-cache and I-cache
  maintenance before control transfer.
- The uploaded program still executes with pre-kernel physical access. Use only
  on a bench with a verified complete SPI backup and an external programmer.

The firmware recovery payload layered on this protocol is documented in
[`payloads/uart-firmware-recovery/README.md`](../payloads/uart-firmware-recovery/README.md).
