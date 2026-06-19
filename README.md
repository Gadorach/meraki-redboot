# Standalone VCore-III LinuxLoader builder

This repository builds a complete 256 KiB VCore-III boot region from source for
Meraki/Vitesse Luton26 and Jaguar-class switches. It is freestanding and does
not require a Linux, OpenWrt, or Buildroot build.

The loader initializes the SoC, DRAM, SPI mapping, caches, and UART; exits boot
mode; validates a 32-byte SPIM payload header at flash offset `0x40000`; copies
the kernel payload to RAM; verifies policy-selected CRC and size constraints;
and enters the declared kernel entry point.

## Source-owned output

The generated boot region contains:

- reset and exception vectors plus the dual-descriptor wrapper;
- two independently placed copies of the same current LinuxLoader build;
- Luton26 and Jaguar-class hardware initialization;
- compile-time CRC and payload-size policy paths;
- optional UART RAM-loader protocol v2;
- no imported executable bootloader body and no post-link binary editing.

Reference binaries may be used by analysis tools to compare structure, but they
are not build inputs. Provenance and reconstruction records are under
`docs/history/`.

## Profiles

| Profile | CRC policy | Legacy size policy | Hard slot boundary |
|---|---|---|---|
| `strict` | reject mismatch | reject above threshold | reject |
| `development` | warn and continue | warn and continue | reject |
| `permissive` | CRC code omitted | threshold omitted | reject |

```sh
make VARIANT=strict
make VARIANT=development
make VARIANT=permissive
make variants
```

Custom policy and postmerkOS kernel-slot geometry:

```sh
make VARIANT=custom \
  CRC_POLICY=warn \
  SIZE_POLICY=hard-only \
  PAYLOAD_SLOT_END=0x00300000
```

The mandatory hard boundary is derived from the loader offset, 32-byte SPIM
header, and configured payload-slot end. The configuration checker rejects any
combination that can cross the next owned flash region.

## UART recovery build

```sh
make all recovery-payloads \
  VARIANT=development \
  UART_RAMLOADER=1 \
  PAYLOAD_SLOT_END=0x00300000
```

The pre-kernel `PMOSRAM2` protocol provides:

- a timer-calibrated probe interval;
- bounded header, frame, and total-transfer states;
- sequence ACK/NACK and retry support;
- frame CRC-32 plus whole-object CRC-32 and SHA-256;
- full load-range and entry-point validation;
- D-cache writeback/invalidate and I-cache invalidate before execution;
- detected SoC-family reporting.

The companion `PMOSPKG2` recovery payloads are built separately for Luton26 and
Jaguar-class SPI register maps. They validate the full release manifest, exact
model, loader digest, firmware layout, flash geometry, JEDEC ID, protection and
status state, and payload descriptor before a nonce-gated full-NOR write.

## Toolchain and outputs

The release path uses `mipsel-linux-gnu-gcc` and matching GNU binutils. On
CachyOS, `make distrobox` creates an Ubuntu 22.04 build environment. Native
Debian/Ubuntu users can run:

```sh
./scripts/install-deps.sh
make -j"$(nproc)" all VARIANT=development UART_RAMLOADER=1
```

Primary outputs:

```text
artifacts/vcoreiii-linuxloader-development.bin
artifacts/vcoreiii-linuxloader-development.bin.sha256
artifacts/vcoreiii-linuxloader-development.bin.manifest.json
payloads/uart-firmware-recovery/artifacts/recovery-luton26.bin
payloads/uart-firmware-recovery/artifacts/recovery-luton26.descriptor.json
payloads/uart-firmware-recovery/artifacts/recovery-jaguar1.bin
payloads/uart-firmware-recovery/artifacts/recovery-jaguar1.descriptor.json
```

The loader image is exactly `0x40000` bytes. The manifest records selected
policies, geometry, source hashes, boot-region SHA-256, and the complete UART
capability contract.

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

The test target covers source policies, payload packing, the actual wrapper-fit
recipe, UART protocol contracts, UART-enabled MIPS32r2 structural builds, and
both recovery payload variants. Physical loader boot and destructive flash
acceptance still require the staged process in
[`docs/HARDWARE-ACCEPTANCE.md`](docs/HARDWARE-ACCEPTANCE.md).

## Documentation

- [`docs/BUILDING.md`](docs/BUILDING.md)
- [`docs/BOOT-REGION-FORMAT.md`](docs/BOOT-REGION-FORMAT.md)
- [`docs/SOURCE-PATCHES.md`](docs/SOURCE-PATCHES.md)
- [`docs/UART-RAMLOADER.md`](docs/UART-RAMLOADER.md)
- [`payloads/uart-firmware-recovery/README.md`](payloads/uart-firmware-recovery/README.md)
- [`docs/HARDWARE-ACCEPTANCE.md`](docs/HARDWARE-ACCEPTANCE.md)
