# Building the standalone loader

## Recommended command

```sh
make all
```

When attached to an interactive terminal, the build asks whether to use
Distrobox. Press Enter for Distrobox or answer `n` for a native build.

```text
Use Distrobox for compilation? [Y/n]:
```

The Distrobox path is recommended on CachyOS and other rolling distributions.
It creates or reuses an Ubuntu 22.04 container named
`postmerkos-vcoreiii-gcc473`. The native and Distrobox paths otherwise use the
same source archives, patches, configuration, validation, and output targets.

Noninteractive selection:

```sh
make all BUILD_MODE=distrobox
make all BUILD_MODE=native
make all BUILD_MODE=auto
```

`BUILD_MODE=auto` selects Distrobox when it is installed and native compilation
otherwise. `make distrobox` remains a compatibility alias for an explicit
Distrobox build.

## Pinned cross-toolchain

The release build does not use the host distribution's current MIPS compiler.
The scripts build and cache a freestanding cross-toolchain from pinned GNU
release sources:

```text
GCC:      4.7.3
binutils: 2.23.2
target:   mipsel-linux-gnu
languages: C only
target libc/headers: none
```

This preserves the embedded MIPS code-generation model used by the original
loader source:

```text
-mno-abicalls -fPIC -G 65535
```

The combination is deliberately compiler-version-sensitive. The build rejects
an unexpected GCC or binutils version unless
`ALLOW_UNVERIFIED_TOOLCHAIN=1` is supplied for a deliberate experiment.

First use downloads and verifies the release archives, builds binutils and the
C compiler into `.work/toolchains/`, and performs a compile probe. Distrobox and
native installations remain separated by host flavor because their host ABIs
may differ. A verified v0.4.0-v0.4.2 installation in the former XDG cache is
automatically copied into `.work/` on first use.
Full source, checksum, patch, cache, and override details are in
[`TOOLCHAIN.md`](TOOLCHAIN.md).

Manual setup targets:

```sh
make deps       # host dependencies plus pinned toolchain
make toolchain  # pinned toolchain only; host dependencies must exist
```

Force a clean toolchain rebuild:

```sh
TOOLCHAIN_REBUILD=1 make toolchain
```

Keep extracted source/build directories for diagnosis:

```sh
TOOLCHAIN_KEEP_BUILD=1 make toolchain
```

## Default build contents

`make all` builds:

1. the selected 256 KiB VCore-III boot region;
2. its map, symbols, disassemblies, hashes, and manifest;
3. the Luton26 UART firmware-recovery payload;
4. the Jaguar1 UART firmware-recovery payload.

The default `development` profile enables the UART RAM loader. Disable it with:

```sh
make all VARIANT=development UART_RAMLOADER=0
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
  UART_RAMLOADER=1 \
  PAYLOAD_SLOT_END=0x00300000
```

The configuration checker validates policy names, alignment, payload limits,
slot boundaries, fallback stride, UART staging limits, and timeout values.

## Source-local work tree

All generated files are placed under `.work/`:

```text
.work/toolchains/                 pinned GCC/binutils installations
.work/toolchain-build/            source-toolchain build log and optional work trees
.work/downloads/                  checksum-verified GNU source archives
.work/build/<variant>/            objects, generated assembly, disassembly, relocations, ELFs
.work/out/<variant>/              inspection copies and complete disassemblies
.work/artifacts/                  flashable boot regions, hashes, manifests
.work/recovery/build/             recovery payload objects and ELFs
.work/recovery/artifacts/         recovery binaries, hashes, descriptors
.work/logs/                       complete build and validation logs
.work/support/                    shareable diagnostic archives
```

Use:

```sh
make work-layout
make support-bundle
```

`make support-bundle` is safe after a failed build and deliberately excludes
the large compiler binaries and extracted GNU source trees.

The loader manifest records the exact compiler/linker identity, source-built
toolchain ID, selected policies, geometry, UART limits, and output hashes.

## Validation

```sh
make test
make test-wrapper-fit
make validate VARIANT=development
make inspect VARIANT=development
```

`make test` runs Python contracts, wrapper-fit arithmetic, MIPS32r2 structural
builds, and Luton26/Jaguar1 recovery-payload structural builds. These structural
builds do not replace the pinned GCC release build.

Every release build also checks:

- exact GCC 4.7.3 and binutils 2.23.2 identity;
- successful compilation of the historical PIC/no-ABI probe;
- mandatory inlining of the CP0/TLB hazard helpers used before DDR;
- absence of stack-pointer references and nested call/link instructions in the
  pre-DDR initialization objects;
- absence of dynamic-loader sections and unresolved symbols;
- absence of relocations in the final linked loader;
- entry point at link address zero;
- dual-copy wrapper fit and exact 256 KiB image size.

## Kernel payload

```sh
make payload \
  KERNEL=/path/to/vmlinuz.bin \
  LOAD_ADDRESS=0x81000000 \
  ENTRY_POINT=0x81000000
```

The packer pads to 32-byte alignment and writes the SPIM header and IEEE CRC-32.
