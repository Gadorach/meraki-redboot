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

The reset-time SoC initialization uses GCC 10 no-ABI, non-PIC, data-free leaf
stages. Assembly computes each stage address from the active loader base and
enters it through `jalr`. Compiler-local J-format jumps are normalized to
PC-relative branches before assembly. Every GNU build rejects pre-DDR stack
access, C calls/J-format jumps, text relocations, unresolved helpers, data,
literals, GOT, and BSS before accepting `loader.elf`.

The post-DDR UART engine is a different fixed-address program. It is linked at
`0xa7f00000`, the uncached KSEG1 alias of the upload-window end. It is embedded
in the flash loader as data, copied into initialized RAM, synchronized, and
entered with a permanent tail jump. It runs the UART
probe and then performs normal SPIM kernel validation/copy/jump itself. This
stage may use ordinary C calls and the established stack without weakening
active/fallback loader relocation. See
[`GCC10-CODEGEN.md`](GCC10-CODEGEN.md).

On CachyOS or another Distrobox host:

```sh
make distrobox
```

The default `development` profile includes the UART RAM loader. Disable it for
an explicitly non-recovery image with `UART_RAMLOADER=0`.

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
  UART_STAGE1_ADDRESS=0xa7f00000 \
  PAYLOAD_SLOT_END=0x00300000
```

Outputs include:

```text
artifacts/vcoreiii-linuxloader-development.bin
artifacts/vcoreiii-linuxloader-development.bin.sha256
artifacts/vcoreiii-linuxloader-development.bin.manifest.json
artifacts/vcoreiii-uart-stage1-development.bin
out/development/uart-stage1-development.elf
out/development/uart-stage1-development.dis
payloads/uart-firmware-recovery/artifacts/recovery-luton26.bin
payloads/uart-firmware-recovery/artifacts/recovery-luton26.descriptor.json
payloads/uart-firmware-recovery/artifacts/recovery-jaguar1.bin
payloads/uart-firmware-recovery/artifacts/recovery-jaguar1.descriptor.json
payloads/uart-smoke-test/artifacts/uart-smoke-test.bin
```


Build the non-destructive RAM execution test separately:

```sh
make uart-smoke-test
```

Upload instructions and expected serial output are in
[`../payloads/uart-smoke-test/README.md`](../payloads/uart-smoke-test/README.md).

The loader manifest records whether UART recovery is compiled in, protocol
version, timeout values, maximum payload bytes, upload range, fixed stage-1
address/size/SHA-256, supported SoC families, transport-integrity algorithms,
and boot-region SHA-256.

## Validation

```sh
make test
make test-wrapper-fit
make validate VARIANT=development
make inspect VARIANT=development
```

`make test` runs Python contracts, positive and negative code-generation
validator fixtures, the real wrapper-fit shell expression, freestanding
MIPS32r2 structural builds with UART enabled, and Luton26/Jaguar1 recovery
payload builds. The GNU GCC 10 cross-build remains the release artifact path.

## Kernel payload

```sh
make payload \
  KERNEL=/path/to/vmlinuz.bin \
  LOAD_ADDRESS=0x81000000 \
  ENTRY_POINT=0x81000000
```

The packer pads to 32-byte alignment and writes the SPIM header and IEEE CRC-32.
