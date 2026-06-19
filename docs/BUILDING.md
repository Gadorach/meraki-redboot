# Building the standalone loader

## Required release toolchain

The authoritative build uses a little-endian MIPS GNU cross toolchain:

- `mipsel-linux-gnu-gcc`
- `mipsel-linux-gnu-ld`
- `mipsel-linux-gnu-objcopy`
- `mipsel-linux-gnu-objdump`
- `mipsel-linux-gnu-nm`
- GNU Make, Python 3, `sha256sum`, and `stat`

No target libc is linked. The loader is freestanding and uses `-nostdlib`.

## Compiler and linker model

The standalone Makefile retains the important Linux 3.18 Kbuild flags:

```text
-EL -mabi=32 -march=mips32r2 -msoft-float
-G 0 -mno-abicalls -fno-pic
-static -n -nostdlib
```

The original loader Makefile adds `-fPIC -G 65535` to the two C initialization
objects; the standalone build preserves that distinction. `head.S` computes its
runtime base with a `bal`/`gp` sequence, allowing the loader body to be packed at
a dynamically calculated offset inside the 256 KiB wrapper.

## Distrobox

From CachyOS or another Distrobox host:

```bash
make distrobox
```

Equivalent explicit invocation:

```bash
DISTROBOX_NAME=postmerkos-vcoreiii-loader \
DISTROBOX_IMAGE=docker.io/library/ubuntu:22.04 \
./scripts/distrobox-build.sh all VARIANT=development
```

The repository should normally be under `$HOME` so it is visible at the same
path inside the container.

## Named profile builds

```bash
make -j"$(nproc)" all VARIANT=strict
make -j"$(nproc)" all VARIANT=development
make -j"$(nproc)" all VARIANT=permissive
make variants
```

Profile mapping:

```text
strict:
  CRC_POLICY=strict
  SIZE_POLICY=legacy-strict

development:
  CRC_POLICY=warn
  SIZE_POLICY=legacy-warn

permissive:
  CRC_POLICY=off
  SIZE_POLICY=hard-only
```

## Custom policy build

```bash
make -j"$(nproc)" all \
  VARIANT=custom \
  CRC_POLICY=warn \
  SIZE_POLICY=hard-only
```

Optional geometry variables:

```text
FALLBACK_REGION_SIZE=0x00400000
PAYLOAD_SLOT_END=FALLBACK_REGION_SIZE
HARD_PAYLOAD_LIMIT=PAYLOAD_SLOT_END-0x00040000-0x20
LEGACY_PAYLOAD_LIMIT=HARD_PAYLOAD_LIMIT
```

The configuration checker rejects a non-power-of-two fallback stride, a payload
slot extending into the next fallback region, unaligned limits, a legacy
threshold above the hard limit, or a hard limit that crosses the payload slot.

Changing `FALLBACK_REGION_SIZE` changes where `loader_fail` searches for the next
fallback loader. `PAYLOAD_SLOT_END` can be changed independently to model an
earlier kernel-partition boundary. For example, a root filesystem beginning at
`0x00300000` requires:

```bash
make VARIANT=development PAYLOAD_SLOT_END=0x00300000
```

which derives a safe hard payload length of `0x002bffe0`.

## OpenWrt toolchain

An OpenWrt-produced compiler may be selected with:

```bash
make CROSS_COMPILE=/absolute/path/to/mipsel-linux-musl- \
     VARIANT=development
```

The prefix must include the trailing hyphen.

## Packaging a kernel payload

The loader build does not depend on a particular kernel. Package the completed
kernel separately:

```bash
make payload \
  KERNEL=/path/to/vmlinuz.bin \
  LOAD_ADDRESS=0x81000000 \
  ENTRY_POINT=0x81000000 \
  PAYLOAD_OUTPUT=artifacts/kernel.vcoreiii-payload.bin
```

The addresses must match the kernel image type and link/decompression entry.
Do not assume they are always identical.

The packer:

1. Reads the completed kernel blob.
2. Pads it to 32-byte alignment.
3. Writes the `0x4d495053` header with CRC field zero.
4. Calculates standard reflected IEEE CRC-32 over header plus padded payload.
5. Writes the calculated CRC into header offset `0x10`.
6. Verifies the completed image.

The CRC is therefore generated after the kernel build but before final firmware
assembly. LinuxLoader itself does not need to be rebuilt for each kernel.

## Validation performed by `make all`

The validator checks:

1. Raw loader is non-empty, word aligned, and begins with the expected `bal`.
2. Flashable wrapper is exactly 262,144 bytes.
3. All five vectors branch to the common dispatcher.
4. Selector/trampoline words match the recovered VCore-III layout.
5. Active and fallback descriptors contain two complete current loader copies.
6. The mandatory hard-size rejection branch is present.
7. Exactly one CRC policy marker and one size policy marker are present.
8. CRC table is absent from `CRC_POLICY=off` builds and present otherwise.
9. UART warning code is present only when a warning policy requires it.

## Reproducibility

`SOURCE_DATE_EPOCH` can be overridden. Manifests record selected policies,
geometry, source hashes, and output hashes. Loader offsets are calculated from
the current `loader.bin` rather than hard-coding historical Ghidra addresses.
