# Pre-kernel UART firmware recovery payload

This freestanding MIPS payload is uploaded through LinuxLoader's `PMOSRAM2`
protocol and implements manifest-bound full-NOR recovery without Linux, MTD,
SSH, or networking.

Two target-specific binaries are produced:

```text
artifacts/recovery-luton26.bin
artifacts/recovery-jaguar1.bin
```

Each binary has a descriptor JSON and SHA-256 sidecar. The descriptor binds:

- protocol version;
- SoC family and family ID;
- SPI software-mode register address;
- exact accepted model list;
- 16 MiB flash geometry;
- accepted JEDEC IDs;
- supported operations and transport-integrity algorithms;
- payload filename, byte count, and SHA-256.

Build both variants with:

```sh
make
```

## Package protocol

The host sends a `PMOSPKG2` header followed by acknowledged `PKF2` frames for:

1. an exact 16 MiB firmware image;
2. its JSON release manifest.

Every frame carries CRC-32. Each complete object is checked with CRC-32 and
SHA-256. The target then enforces all of the following before it can present an
erase challenge:

- target family and exact model are accepted by the compiled payload;
- the release manifest marks the model `validated`, `confirmed`, or explicitly
  acknowledged `untested`;
- artifact size, SHA-256, boot-chain identifier, SPIM kernel signature, and
  SquashFS signature match the staged image;
- the image contains a protocol-v2 UART-capable loader whose 256 KiB digest
  matches the manifest;
- the manifest enables UART firmware recovery protocol v2 and declares the
  requested operation and integrity algorithms;
- payload family, SPI register, exact model list, payload record, flash
  geometry, and loader SoC-family record are consistent;
- the detected JEDEC ID is present in both the compiled device table and the
  release manifest;
- the flash is 16 MiB, uses the declared erase/page geometry, is not busy or
  block-protected, and reports no device-specific error flags.

## Operations

- `verify` is host-only and never opens the serial port.
- `dry-run` uploads the payload, image, and manifest and performs target-side
  validation and flash preflight without erase or program commands.
- `flash` requires a target-generated nonce. The host must return the exact
  `ERASEFLASH <nonce>` line before any erase begins.

Erase and program operations verify write-enable state, use bounded device
operation timeouts, inspect completion/error status, verify each erased block,
and perform a full byte-for-byte readback after programming. The target emits a
deterministic terminal success, abort, or error line.

The image transfer has a 45-minute overall deadline, each frame is bounded, and
the confirmation line has a 60-second deadline. This path always overwrites the
complete 16 MiB device. Keep an external SPI programmer connected only while
the switch is unpowered, and retain verified backups.
