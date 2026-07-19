# Standalone VCore-III LinuxLoader builder

This repository builds a complete 256 KiB VCore-III boot region from source for
Meraki/Vitesse Luton26 and Jaguar-class switches. It is freestanding and does
not require a Linux, OpenWrt, or Buildroot build.

The loader initializes the SoC, DRAM, SPI mapping, caches, and UART; exits boot
mode; copies a fixed-RAM stage containing a boot menu and both platform recovery
programs; offers a three-second menu trigger followed by a five-second explicit
selection window; then either runs the UART executable uploader, launches the
matching firmware recovery, or validates and boots the SPIM kernel at flash
offset `0x40000`.

## Source-owned output

The generated boot region contains:

- reset and exception vectors plus the dual-descriptor wrapper;
- two independently placed copies of the same compact LinuxLoader build;
- one immutable fixed-RAM UART stage shared by both loader copies;
- Luton26 and Jaguar-class hardware initialization;
- compile-time CRC and payload-size policy paths;
- an optional fixed-RAM stage containing the UART uploader and both platform-specific firmware recovery images;
- no imported executable bootloader body and no post-link binary editing.

Reference binaries may be used by analysis tools to compare structure, but they
are not build inputs. Provenance and reconstruction records are under
`docs/history/`.

## Normal build

Run:

```sh
make all
```

On an interactive terminal, `make all` asks:

```text
Use Distrobox for compilation? [Y/n]:
```

Press Enter or answer `y` to use the managed Ubuntu 22.04 Distrobox. Answer `n`
to build natively. Both paths download, checksum, build, cache, and verify the
same pinned freestanding cross-toolchain:

```text
GNU GCC 4.7.3
GNU binutils 2.23.2
target: mipsel-linux-gnu
```

The toolchain and every generated build product are kept under the source tree's
`.work/` directory. A verified toolchain left by v0.4.0-v0.4.2 in the old XDG
cache is imported automatically on first use, avoiding an unnecessary rebuild.
No target C library is built or used. See [`docs/TOOLCHAIN.md`](docs/TOOLCHAIN.md)
for source hashes, work-tree paths, overrides, and rebuild controls.

For scripts or CI, bypass the prompt explicitly:

```sh
make all BUILD_MODE=distrobox
make all BUILD_MODE=native
make all BUILD_MODE=auto
```

The historical explicit target remains available:

```sh
make distrobox
```

A successful default build produces the selected boot-region image and both
Luton26/Jaguar1 recovery programs; UART-enabled loaders embed both programs.

If a v0.4.0 bootstrap stopped while generating `bfd.info`, force a clean
toolchain retry with:

```sh
TOOLCHAIN_REBUILD=1 make all
```

The checksum-verified source downloads remain cached.

## Profiles

| Profile | CRC policy | Legacy size policy | UART RAM loader | Hard slot boundary |
|---|---|---|---|---|
| `strict` | reject mismatch | reject threshold excess and unaligned size | disabled | reject |
| `development` | report mismatch and continue | warn, round unaligned size to 32 bytes, and continue | enabled | reject |
| `permissive` | CRC code omitted | omit legacy threshold; warn and round unaligned size | disabled | reject |

```sh
make all VARIANT=strict
make all VARIANT=development
make all VARIANT=permissive
make variants
```

Custom policy and postmerkOS kernel-slot geometry:

```sh
make all \
  VARIANT=custom \
  CRC_POLICY=warn \
  SIZE_POLICY=hard-only \
  UART_RAMLOADER=1 \
  PAYLOAD_SLOT_END=0x00300000
```

The mandatory hard boundary is derived from the loader offset, 32-byte SPIM
header, and configured payload-slot end. The configuration checker rejects any
combination that can cross the next owned flash region.

## UART recovery build

The default `development` profile includes the UART RAM loader. Pass
`UART_RAMLOADER=0` to produce a development image without it. The UART engine
is deliberately **not** linked as callable C inside the relocatable flash
loader. Instead it is linked at `0xa7f00000`, stored once at boot-region offset
`0x20000`, copied through
its uncached KSEG1 address after DDR and the stack are live, and entered with a
non-returning assembly `jr`. Stage 1 owns all remaining boot work, including
normal flash-kernel validation/copy and final kernel entry. This avoids the direct `J/JAL` and absolute-literal behavior of
the historical GCC 4.7 PIC/no-ABI combination. The pre-kernel `PMOSRAM2`
protocol provides:

- a timer-calibrated probe interval;
- bounded header, frame, and total-transfer states;
- sequence ACK/NACK and retry support;
- frame CRC-32 plus whole-object CRC-32 and SHA-256;
- full load-range and entry-point validation;
- D-cache writeback/invalidate and I-cache invalidate before execution;
- detected SoC-family reporting.

The companion PMOSREC v3 recovery payloads are built separately for Luton26 and
Jaguar-class SPI register maps and embedded into every UART-enabled stage. The
RAM-loader bootstrap remains at 115200 baud; PMOSREC then qualifies target-generated
baud rates, 4 KiB framed transfer, compact CRC-protected acknowledgements,
sparse reconstruction and independent LZ4 blocks. It validates the manifest
before the image and verifies the complete reconstructed 16 MiB SHA-256 before
the destructive confirmation and full-NOR write path.


## PMOSREC v3 adaptive recovery

The boot menu and `PMOSRAM2` executable upload remain fixed at 115200 baud. The
RAM-resident PMOSREC v3 stage negotiates the nearest target-generated UART rate,
qualifies it bidirectionally with deterministic CRC streams, and independently
rolls back on failure. Normal host policy then qualifies 4 KiB frames with a
one-frame production window, compact cumulative ACKs, sparse reconstruction and
LZ4 blocks. The manifest is validated before the image and the complete
reconstructed image is SHA-256 verified before erase authorization.

The target still supports windows up to 16 for paced engineering diagnostics.
Production remains at window 1 because the recovery UART has no RTS/CTS flow
control and must finish CRC/decode/copy work before another frame header arrives.

See `payloads/uart-firmware-recovery/README.md` for the complete protocol and
integrity contract.

Primary outputs:

```text
.work/artifacts/vcoreiii-linuxloader-development.bin
.work/artifacts/vcoreiii-linuxloader-development.bin.sha256
.work/artifacts/vcoreiii-linuxloader-development.bin.manifest.json
.work/recovery/artifacts/recovery-luton26.bin
.work/recovery/artifacts/recovery-luton26.descriptor.json
.work/recovery/artifacts/recovery-jaguar1.bin
.work/recovery/artifacts/recovery-jaguar1.descriptor.json
```

Diagnostic products are retained alongside them:

```text
.work/build/development/*.o
.work/build/development/*.o.s
.work/build/development/*.o.dis
.work/build/development/*.o.relocs
.work/build/development/uart-stage1/uart-stage1.elf
.work/build/development/uart-stage1/uart-stage1.bin
.work/build/development/uart-stage1/uart-stage1.elf.dis
.work/logs/build-distrobox.log
.work/logs/loader-codegen-validation-development.log
.work/logs/uart-stage1-validation-development.log
```

Run `make support-bundle` after either a successful or failed build to create a
small shareable archive containing objects, disassemblies, relocation tables,
manifests, and logs without bundling the large compiler binaries.

The loader image is exactly `0x40000` bytes. The manifest records selected
policies, geometry, source hashes, toolchain identity and versions,
boot-region SHA-256, and the UART capability contract.

### Expected development-image serial sequence

With no input, stage 1 prints the probe marker, waits three seconds, and continues
normal boot:

```text
Low level initialization complete, exiting boot mode
PMOSRAM STAGE1 COPY
PMOSBOOT CONTEXT LOADER=... PAYLOAD=40040000 FALLBACK=40400000
PMOSBOOT MENU-PROBE TIMEOUT_MS=00000bb8
PMOSBOOT PASS-MENU-PROBE: NO INPUT; CONTINUING NORMAL BOOT
PMOSBOOT PASS-HEADER-ADDRESS: ADDRESS: 0x40040000
...
PMOSBOOT PASS-CRC: EXPECTED: ... | GOT: ...
PMOSBOOT PASS-EXEC: ENTRY: 0x81000000
```

Any byte during the probe opens the menu. The trigger byte is discarded; a new
explicit option must arrive within five seconds:

```text
PMOSBOOT MENU 1=UART-RAMLOADER 2=FW-RECOVERY
PMOSBOOT MENU-READY TIMEOUT_MS=00001388
```

`1` enters a persistent `PMOSRAM READY 2` upload listener. `2` copies and runs
the embedded recovery matching the detected SoC. `3` copies and runs the
embedded Jaguar1 PMOSLIVE payload. Invalid input or no explicit
selection prints `WARN-MENU-*` and resumes normal boot. Fatal flash-image checks
print the exact `FAIL-*` values and automatically launch the matching embedded
recovery rather than chaining to the historical flash fallback.


### Historical unaligned kernel compatibility

The original 32-byte assembly copy loop rounded an unaligned declared payload
size up implicitly. Development and permissive builds now reproduce that
behavior explicitly: they emit `WARN-SIZE-ALIGN`, calculate an effective size
with `(declared + 31) & ~31`, and use that effective size for hard-boundary,
overlap, CRC, copy, and cache checks. Strict builds emit `FAIL-SIZE-ALIGN` and
fall back. Zero length and every hard safety boundary remain unconditional
failures.

## Kernel payload packaging

```sh
make payload \
  KERNEL=/path/to/vmlinuz.bin \
  LOAD_ADDRESS=0x81000000 \
  ENTRY_POINT=0x81000000
```

`tools/mkvcoreiii_payload.py` pads the kernel to a 32-byte boundary, creates the
SPIM header, calculates IEEE CRC-32 over the zeroed-CRC header and padded
payload, and verifies the result.

## Validation

```sh
make test
make test-wrapper-fit
make inspect VARIANT=development
make validate VARIANT=development
```

The test target covers source policies, payload packing, wrapper-fit arithmetic,
UART protocol contracts, MIPS32r2 structural builds, and both recovery payload
variants. A release build additionally verifies the pinned compiler and linker,
checks that pre-DDR initialization is a single forced-inline leaf with no
stack references or nested call/link instructions, rejects unresolved/dynamic
linkage, and requires the final loader to contain no remaining relocations.
The CP0/TLB compatibility helpers are explicitly `always_inline`; ordinary
`static inline` was insufficient under GCC 4.7.3 with `-Os` and caused the
observed pre-DDR ABI frame.
Physical loader boot and destructive flash acceptance still require the staged
process in [`docs/HARDWARE-ACCEPTANCE.md`](docs/HARDWARE-ACCEPTANCE.md).

## Documentation

- [`docs/BUILDING.md`](docs/BUILDING.md)
- [`docs/TOOLCHAIN.md`](docs/TOOLCHAIN.md)
- [`docs/BOOT-REGION-FORMAT.md`](docs/BOOT-REGION-FORMAT.md)
- [`docs/SOURCE-PATCHES.md`](docs/SOURCE-PATCHES.md)
- [`docs/UART-RAMLOADER.md`](docs/UART-RAMLOADER.md)
- [`docs/RAMLOADER-HARDWARE-TEST.md`](docs/RAMLOADER-HARDWARE-TEST.md)
- [`payloads/uart-firmware-recovery/README.md`](payloads/uart-firmware-recovery/README.md)
- [`docs/HARDWARE-ACCEPTANCE.md`](docs/HARDWARE-ACCEPTANCE.md)
