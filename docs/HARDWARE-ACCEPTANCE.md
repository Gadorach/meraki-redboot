# Hardware acceptance plan

Automated structural tests do not replace target testing. Use a device with a
verified 16 MiB SPI backup, external programmer access, 3.3 V UART, and stable
power.

## Loader and permanent stage-1 boot

1. Boot the development image without serial input.
2. Confirm the complete early-init sequence and `PMOSMENU PROBE`.
3. After approximately three seconds with no input, confirm `PMOSBOOT FLASH
   HEADER=...` and `PMOSRAM FLASH-BOOT ...`, followed by the normal kernel.
4. Send one random byte during the probe but do not select an option. Confirm
   the five-second `PMOSMENU TIMEOUT` and normal flash boot.
5. Interrupt the probe, explicitly select `1`, allow `HEADER-TIMEOUT`, and
   confirm the session returns to flash boot.
6. Interrupt the probe, explicitly select `2`, and confirm the detected
   platform's `PMOSREC READY 2` banner. Do not send a flash package yet.
7. Boot an invalid zero-length or hard-oversize flash payload and confirm
   `PMOSBOOT FLASH-FAIL ...` enters a persistent menu rather than jumping 4 MiB
   forward. Separately verify that a known legacy kernel with a non-32-byte
   declared size is copied exactly and reaches its entry point.
8. Verify active/fallback descriptors and hashes against the manifest.
9. Repeat strict/permissive policy tests after the development baseline boots.

## Non-destructive UART RAM execution

1. Build `make uart-smoke-test`.
2. Upload the generated 132-byte (compiler-dependent size may vary) payload
   using the command in `payloads/uart-smoke-test/README.md`.
3. Confirm `PMOSRAM VERIFIED`, `PMOSRAM EXEC 81000000`, and
   `PMOSRAM SMOKE OK`.
4. Confirm the payload returns and stage 1 prints `PMOSRAM RETURNED` followed by
   `PMOSRAM FLASH-BOOT ...` and a normal kernel boot.
5. Interrupt header/frame transfers and confirm bounded abort followed by the
   same stage-1 flash boot.
6. Exercise NACK/retry and duplicate-final-frame handling.

## Recovery dry-run and destructive acceptance

Before any write, confirm exact model, family, full manifest, image and loader
digests, payload descriptor, JEDEC ID, protection state, and geometry. Dry-run
must issue no erase/program command.

Proceed destructively only with an external restore path. Capture erase,
program, and full readback progress; compare a complete external flash read to
the selected artifact; then verify loader, kernel, rootfs, identity, management,
ports, LEDs, and PoE where applicable.

Record the board model, flash JEDEC ID, loader and firmware manifest SHA-256,
serial adapter, baud rate, compiler identity, and complete logs.
