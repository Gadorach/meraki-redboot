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
- optional UART RAM-loader protocol v2 using an embedded fixed-RAM stage 1;
- non-executable embedded Luton26 and Jaguar1 firmware-recovery payloads;
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
make all \
  VARIANT=development \
  UART_RAMLOADER=1 \
  PAYLOAD_SLOT_END=0x00300000
make recovery-payloads
```

Both commands automatically use Distrobox when the host cross-toolchain is not
installed. `make all` now builds and embeds both target-specific recovery
payloads automatically; `make recovery-payloads` remains available for separate
payload inspection.

The `development` profile enables UART recovery by default; pass
`UART_RAMLOADER=0` to produce a development image without it. The relocatable
flash loader copies a separately linked protocol engine to the uncached
`0xa7f00000` address before execution. Stage 1 first offers a three-second
interrupt window. Any byte opens a five-second explicit menu:

```text
1 = chunked RAM loader
2 = platform-selected firmware recovery
```

No selection continues ordinary flash-kernel boot. Fatal flash-header, mandatory
alignment/range, strict-policy, or kernel-return failures enter the same menu
persistently instead of chaining to an unknown 4 MiB flash region. The
pre-kernel `PMOSRAM2` protocol provides:

- a timer-calibrated probe interval;
- bounded header, frame, and total-transfer states;
- sequence ACK/NACK and retry support;
- frame CRC-32 plus whole-object CRC-32 and SHA-256;
- full load-range and entry-point validation;
- D-cache writeback/invalidate and I-cache invalidate before execution;
- detected SoC-family reporting.

The companion `PMOSPKG2` recovery payloads are built for Luton26 and Jaguar-class
SPI register maps and embedded as non-executable stage-1 data. Option 2 copies
only the detected platform's image to `0x81000000`, performs cache maintenance,
and enters it. The recovery image validates the full release manifest, exact
model, loader digest, firmware layout, flash geometry, JEDEC ID, protection and
status state, and payload descriptor before a nonce-gated full-NOR write.

## Toolchain and outputs

The release path uses GCC 10 `mipsel-linux-gnu-gcc` and matching GNU binutils.
The pre-DDR initialization sources are force-inlined into call-free, stackless,
GP-relative objects; assembly enters them with PC-relative `bal`. See
[`docs/GCC10-CODEGEN.md`](docs/GCC10-CODEGEN.md). On CachyOS, plain `make`/`make all` automatically uses the existing Ubuntu
22.04 Distrobox when the GNU MIPS cross-toolchain is absent from the host.
`make distrobox` explicitly selects that path. Native Debian/Ubuntu users can
run:

```sh
./scripts/install-deps.sh
make -j"$(nproc)" all VARIANT=development UART_RAMLOADER=1
```

Primary outputs:

```text
artifacts/vcoreiii-linuxloader-development.bin
artifacts/vcoreiii-linuxloader-development.bin.sha256
artifacts/vcoreiii-linuxloader-development.bin.manifest.json
artifacts/vcoreiii-uart-stage1-development.bin
artifacts/vcoreiii-recovery-luton26.bin
artifacts/vcoreiii-recovery-jaguar1.bin
payloads/uart-firmware-recovery/artifacts/recovery-luton26.descriptor.json
payloads/uart-firmware-recovery/artifacts/recovery-jaguar1.descriptor.json
```

The boot-region image is exactly `0x40000` bytes. Its two relocatable loader
copies each contain the same validated stage-1 image, including byte-identical
non-executable recovery payloads. The manifest records the stage-1 fixed
address, menu timeout, payload sizes and SHA-256 values, section-execution
policy, selected validation policies, geometry, source hashes, and boot-region
SHA-256.

The development profile is CRC-warn and legacy-size-warn. Mandatory structural
checks remain hard failures in every profile: nonzero payload length, hard slot
boundary, valid KSEG0 load address, arithmetic overflow, stack and stage-1
overlap. Fixed-RAM stage 1 copies the exact declared byte count, so legacy
kernels whose size is not a multiple of 32 can boot safely. `VARIANT=permissive`
disables CRC calculation and the legacy threshold, but intentionally does not
disable the hard safety checks.

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
- [`docs/GCC10-CODEGEN.md`](docs/GCC10-CODEGEN.md)
- [`docs/BOOT-REGION-FORMAT.md`](docs/BOOT-REGION-FORMAT.md)
- [`docs/SOURCE-PATCHES.md`](docs/SOURCE-PATCHES.md)
- [`docs/UART-RAMLOADER.md`](docs/UART-RAMLOADER.md)
- [`payloads/uart-firmware-recovery/README.md`](payloads/uart-firmware-recovery/README.md)
- [`docs/HARDWARE-ACCEPTANCE.md`](docs/HARDWARE-ACCEPTANCE.md)
