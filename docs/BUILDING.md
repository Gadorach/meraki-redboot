# Building the standalone loader

## Toolchain

The release build uses little-endian MIPS GNU tools:

```text
mipsel-linux-gnu-gcc
mipsel-linux-gnu-ld
mipsel-linux-gnu-objcopy
mipsel-linux-gnu-objdump
mipsel-linux-gnu-nm
```

GNU Make, Python 3, `sha256sum`, and `stat` are also required. The loader and
recovery payloads are freestanding and link without a target libc.

On CachyOS or another Distrobox host:

```sh
make distrobox
```

On Debian or Ubuntu:

```sh
./scripts/install-deps.sh
make -j"$(nproc)" all VARIANT=development
```

## Profiles and geometry

```sh
make all VARIANT=strict
make all VARIANT=development
make all VARIANT=permissive
make variants
```

Custom policy and layout example:

```sh
make all \
  VARIANT=custom \
  CRC_POLICY=warn \
  SIZE_POLICY=hard-only \
  PAYLOAD_SLOT_END=0x00300000
```

The configuration checker validates policy names, alignment, payload limits,
slot boundaries, fallback stride, UART staging limits, and timeout values.

## UART-capable build

```sh
make all recovery-payloads \
  VARIANT=development \
  UART_RAMLOADER=1 \
  PAYLOAD_SLOT_END=0x00300000
```

Outputs include:

```text
artifacts/vcoreiii-linuxloader-development.bin
artifacts/vcoreiii-linuxloader-development.bin.sha256
artifacts/vcoreiii-linuxloader-development.bin.manifest.json
payloads/uart-firmware-recovery/artifacts/recovery-luton26.bin
payloads/uart-firmware-recovery/artifacts/recovery-luton26.descriptor.json
payloads/uart-firmware-recovery/artifacts/recovery-jaguar1.bin
payloads/uart-firmware-recovery/artifacts/recovery-jaguar1.descriptor.json
```

The loader manifest records whether UART recovery is compiled in, protocol
version, timeout values, maximum payload bytes, DRAM range, supported SoC
families, transport-integrity algorithms, and boot-region SHA-256.

## Validation

```sh
make test
make test-wrapper-fit
make validate VARIANT=development
make inspect VARIANT=development
```

`make test` runs Python contracts, the real wrapper-fit shell expression,
freestanding MIPS32r2 structural builds with UART enabled, and Luton26/Jaguar1
recovery-payload structural builds. The GNU cross-build remains the release
artifact path.

## Kernel payload

```sh
make payload \
  KERNEL=/path/to/vmlinuz.bin \
  LOAD_ADDRESS=0x81000000 \
  ENTRY_POINT=0x81000000
```

The packer pads to 32-byte alignment and writes the SPIM header and IEEE CRC-32.
