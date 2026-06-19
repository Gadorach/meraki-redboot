# UART recovery write enablement update

Changes made after the initial UART RAM-loader package:

- Enabled destructive full SPI NOR erase/program support in `payloads/uart-firmware-recovery/recovery.c`.
- Added an explicit serial warning and `ERASEFLASH` confirmation gate before erase begins.
- Added generic bit-banged SPI NOR commands for JEDEC ID read, write-enable, 64 KiB block erase, 256-byte page program, status polling, and SPI readback verification.
- Kept the payload constrained to exact 16 MiB full-flash images and package checksum validation before any write prompt is shown.
- Updated the host bootloader recovery helper to forward `ERASEFLASH` only after a matching local prompt, with optional `--auto-confirm-erase` for scripted bench testing.
- Documented the default Jaguar1/MS42-class SPI software-mode register and the Luton26/MS220 override: `CFLAGS_EXTRA=-DSPI_SW_MODE_ADDR=0x70000064`.

Safety note: this remains development/recovery-only code intended for bench use with an external SPI programmer and known-good backup.

## Protocol-v2 review remediation — 2026-06-19

The initial write-enabled implementation was replaced by a bounded,
manifest-bound recovery contract. The loader now accepts a complete CRC- and
SHA-protected header/frame stream, uses a calibrated CP0 timer, validates the
full DRAM interval, performs explicit cache maintenance, and returns to normal
boot after malformed or interrupted input. Duplicate retransmissions must match
the exact most recently accepted frame.

The recovery payload was split into Luton26 and Jaguar-class builds. It now
validates the release manifest, exact model partition, loader identity, image
layout, payload record, flash geometry, JEDEC identity, protection state,
write-enable state, device error flags, and operation timeouts. Verify is
host-only, dry-run cannot erase, and flash requires a target-generated nonce.
The host tools use acknowledged, short-write-safe framing and deterministic
terminal results. A completion message is accepted as the final ACK when the
explicit final ACK is lost.
