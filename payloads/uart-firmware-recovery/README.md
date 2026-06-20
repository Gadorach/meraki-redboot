# PMOSREC v3 pre-kernel UART firmware recovery

This freestanding MIPS payload is launched by LinuxLoader after either an
embedded recovery selection or a verified `PMOSRAM2` executable upload. It
implements manifest-bound full-NOR recovery without Linux, MTD, SSH or
networking.

Two target-specific binaries are produced:

```text
artifacts/recovery-luton26.bin
artifacts/recovery-jaguar1.bin
```

Each binary has a descriptor JSON and SHA-256 sidecar. The descriptor binds the
SoC family, SPI register map, exact accepted models, flash geometry, JEDEC IDs,
entry contract, manifest parser, adaptive transport and integrity algorithms.

## Stable bootstrap

The meraki-redboot menu and RAM-loader executable transfer always remain at
115200 baud. PMOSREC begins adaptive negotiation only after its executable has
passed RAM-loader CRC-32 and SHA-256 validation and entered at `0x81000000`.

## Protocol v3

PMOSREC reports `PMOSREC READY 3` and a `PMOSRECOVERY3` descriptor. The host
proposes UART rates and the target returns the nearest rate its current UART
clock and divisor can generate. Every candidate is qualified in both directions
with deterministic pseudorandom streams and known CRC-32 values. Binary ACKs and
target-to-host test streams use a byte-transparent UART writer; CRLF conversion
is restricted to human-readable console lines.

Both sides independently retain the previous known-good rate. A failed
candidate causes the target to restore its previous divisor and emit fallback
beacons. The host restores the same rate independently. The target keeps support
for arbitrary host proposals; normal host policy may use a short conventional
rate list, while engineering tools may still perform refinement scans.

Preferred transfer parameters are:

- 4096-byte decoded frames, with 1024-byte fallback;
- windows of 16, 8, 4 or 1 frames;
- binary cumulative ACK/NAK records;
- selective retry bitmap;
- CRC-32 for frame headers, wire bytes, decoded bytes and ACK records.

A lost one-byte ACK confirmation cannot corrupt the next frame: PMOSREC accepts
and preserves the first byte of the next frame magic in a pushback slot when
necessary.

## Sparse and LZ4 representations

At the selected rate, the host qualifies raw, sparse, LZ4 and sparse-LZ4
transports using a synthetic object. Optional modes that fail are disabled
without ending recovery.

LZ4 is block-independent. Each frame carries both compressed-wire and decoded
CRC-32 values. Sparse frames omit complete `0xff` ranges. PMOSREC initializes
the 16 MiB staging buffer to `0xff`, applies received extents, and then verifies
CRC-32 and SHA-256 over the complete reconstructed image.

## Manifest-first order

The release manifest is transferred before the firmware object. PMOSREC checks
model compatibility, SoC family, image size, loader/recovery contracts, flash
geometry, JEDEC allow-list and declared image digest before accepting the large
payload.

The image cannot reach erase authorization until:

- every frame and compact ACK has passed CRC-32;
- manifest and image objects have passed CRC-32 and SHA-256;
- the reconstructed 16 MiB image matches the authoritative digest;
- SPIM kernel and SquashFS layout checks pass;
- flash status, protection and device-specific error checks pass.

## Confirmation and reset

After validation, PMOSREC emits `ERASEFLASH <nonce>`. Incorrect confirmation
text does not terminate the session; the stage repeats the expected command and
waits indefinitely. Power cycling cancels.

After erase, program and complete readback verification, PMOSREC prints a
five-second countdown, requests the family-specific SoC soft-chip reset, and arms the family-specific ICPU watchdog if execution continues.

## Hardware preflight

Before accepting commands, PMOSREC enables and verifies the SPI controller,
reads JEDEC ID, status and SFDP, and rejects a no-response bus.

The `PMOS3 PREFLIGHT` operation additionally:

1. CRC-checks the complete 256 KiB bootloader region;
2. backs up one non-loader 64 KiB sector;
3. erases and verifies it;
4. programs and verifies every page;
5. erases and restores the original sector;
6. verifies the restored sector and unchanged bootloader CRC-32.

The target rejects any scratch sector below `0x00040000`. The default is
`0x00ff0000`.

## Current contracts

```text
format=postmerkos.uart-recovery-payload.v3
protocol_version=3
entry_contract=flat-binary-byte-zero-v1
manifest_lookup_contract=direct-object-members-v1
hardware_preflight_contract=spi-nor-scratch-rw-restore-loader-crc-v4
adaptive_transport_contract=pmosrec-v3-adaptive-uart-sparse-lz4-v1
```
