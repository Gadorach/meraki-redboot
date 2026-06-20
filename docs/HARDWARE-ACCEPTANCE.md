# Hardware acceptance plan

Automated structural tests do not replace target testing. Use a device with a
verified 16 MiB SPI backup, external programmer access, 3.3 V UART, and a stable
power source.

## Loader boot acceptance

1. Capture the complete serial boot log with the source-built strict profile.
2. Confirm the reported SoC family and normal SPIM kernel boot.
3. Verify the active and fallback descriptors from `make inspect` against the
   generated manifest.
4. Repeat with the development and permissive profiles using payloads with
   correct and deliberately incorrect CRC values while staying inside the hard
   payload boundary.
5. Confirm that oversized, zero-length, unaligned, and boundary-crossing SPIM
   payloads are rejected.

## UART RAM-loader acceptance

1. Boot a UART-enabled image without host input and capture the exact sequence:
   `STAGE1 COPY`, `MENU-PROBE`, approximately three seconds without input,
   structured `PASS/WARN/FAIL/SKIP` image diagnostics, and `PMOSBOOT PASS-EXEC` before normal kernel output.
2. If a marker is missing, preserve `.work/build/<variant>/uart-stage1/`, the
   loader disassembly, and `.work/logs/` with `make support-bundle`.
3. Trigger the menu and provide no valid option; confirm the five-second menu timeout and normal boot.
4. Select option 1, interrupt header/frame transfers, and confirm bounded abort followed by the persistent retry listener.
5. Exercise frame NACK/retry and a duplicate final accepted frame.
6. Load a non-destructive diagnostic payload on both Luton26 and Jaguar-class
   targets and confirm cache-safe execution. If it returns, confirm stage 1
   resumes the flash-kernel path deterministically.
7. Select option 2 on each family and confirm the embedded recovery selected matches the detected SoC.
8. Confirm the uploaded payload range remains below `0x87f00000` and the
   embedded UART engine reports fixed execution at `0xa7f00000`.

## Recovery dry-run acceptance

1. Run host-only `verify` against each exact target model.
2. Run `dry-run`; confirm image, manifest, payload descriptor, JEDEC ID, flash
   status, model, family, geometry, and all digests are checked.
3. Confirm no erase or page-program command is issued in dry-run.
4. Exercise unknown JEDEC, protected status, stale Micron flag status, malformed
   manifest, wrong payload digest, wrong model, and unsupported operation cases.

## Destructive recovery acceptance

Proceed only after dry-run passes and an external programmer can restore the
board.

1. Confirm the host full-flash authorization and target nonce are both required.
2. Capture erase, program, and full readback progress.
3. Verify the terminal success line and power-cycle instruction.
4. Read the complete flash with the external programmer and compare it to the
   selected artifact.
5. Boot and verify loader, kernel, rootfs, model identity, management access,
   ports, LEDs, and PoE where applicable.
6. Test controlled interruption only on expendable hardware with a proven
   external restore procedure.

Record exact board model, flash JEDEC ID, loader manifest SHA-256, firmware
manifest SHA-256, serial adapter, baud rate, and all logs for each run.
