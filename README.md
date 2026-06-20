# Standalone VCore-III LinuxLoader builder

This repository builds a complete 256 KiB VCore-III boot region from source for
Meraki/Vitesse Luton26 and Jaguar-class switches. It is freestanding and does
not require a Linux, OpenWrt, or Buildroot build.

The loader initializes the SoC, DRAM, SPI mapping, caches, and UART; exits boot
mode; copies an optional UART engine into a fixed uncached RAM window; offers
the recovery upload interval from that RAM stage; validates a 32-byte SPIM
payload header at flash offset `0x40000`; then leaves the fixed-RAM stage in
control to validate and copy the kernel, apply the selected CRC and size
policies, and enter the declared kernel entry point.

## Source-owned output

The generated boot region contains:

- reset and exception vectors plus the dual-descriptor wrapper;
- two independently placed copies of the same current LinuxLoader build;
- Luton26 and Jaguar-class hardware initialization;
- compile-time CRC and payload-size policy paths;
- an optional embedded UART stage-1 image, copied to fixed uncached RAM before running protocol v2;
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
Luton26/Jaguar1 UART recovery programs.

If a v0.4.0 bootstrap stopped while generating `bfd.info`, force a clean
toolchain retry with:

```sh
TOOLCHAIN_REBUILD=1 make all
```

The checksum-verified source downloads remain cached.

## Profiles

| Profile | CRC policy | Legacy size policy | UART RAM loader | Hard slot boundary |
|---|---|---|---|---|
| `strict` | reject mismatch | reject above threshold | disabled | reject |
| `development` | warn and continue | warn and continue | enabled | reject |
| `permissive` | CRC code omitted | threshold omitted | disabled | reject |

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
loader. Instead it is linked at `0xa7f00000`, embedded as data, copied through
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

The companion `PMOSPKG2` recovery payloads are built separately for Luton26 and
Jaguar-class SPI register maps. They validate the release package, exact model,
loader digest, firmware layout, flash geometry, JEDEC ID, protection and status
state, and payload descriptor before the destructive confirmation and full-NOR
write path.

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

After the low-level initialization message, a UART-enabled build should emit:

```text
Low level initialization complete, exiting boot mode
PMOSRAM STAGE1 COPY
PMOSRAM READY 2 SOC=jaguar1 ...
```

If no UART byte arrives during the probe interval, stage 1 remains in control
and continues directly into the flash-kernel path:

```text
PMOSBOOT UART-DONE
PMOSBOOT FLASH HEADER=40040000
PMOSBOOT KERNEL LOAD=81000000 SIZE=... ENTRY=81000000
PMOSBOOT EXEC 81000000
```

A malformed or incomplete UART header prints `PMOSRAM ABORT ...` and then uses
the same flash-kernel path. A stop before `STAGE1 COPY` points to boot-mode
exit/remap; a stop after `STAGE1 COPY` but before `READY` points to the copy or
fixed-RAM entry; a `PMOSBOOT FAIL ...` line identifies header, geometry, CRC, or
fallback selection failures explicitly.

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
