# Pinned legacy MIPS toolchain

## Purpose

LinuxLoader's original C build uses an embedded MIPS PIC model combining:

```text
-mno-abicalls -fPIC -G 65535
```

Later GCC releases reject or materially change this model. The project therefore
builds an isolated C-only cross-compiler from GNU GCC 4.7.3 and GNU binutils
2.23.2 sources instead of depending on whatever MIPS compiler a host
distribution currently packages.

## Locked inputs

The canonical values are stored in `scripts/toolchain-config.sh`.

```text
GCC archive: gcc-4.7.3.tar.bz2
GCC SHA-512:
5671a2dd3b6ac0d23f305cb11a796aebd823c1462b873136b412e660966143f4e07439bd8926c1443b78442beb6ae370ef91d819ec615920294875b722b7b0bd

binutils archive: binutils-2.23.2.tar.bz2
binutils SHA-256:
fe914e56fed7a9ec2eb45274b1f2e14b0d8b4f41906a5194eac6883cfe5c1097
```

Downloads are attempted from configured GNU/GCC mirrors. A mismatched archive is
deleted and never extracted. For an offline build, place both correctly named archives in:

```text
<project>/.work/downloads
```

or in `$POSTMERKOS_WORK_ROOT/downloads` when a custom work root is selected.
The normal checksum verification still runs.

## Release-source choice

The installer uses the official release tarball rather than a Git checkout. The
release archive already contains generated parser, configure, and documentation
outputs with release-appropriate timestamps, so the build does not invoke
`contrib/gcc_update` or regenerate the source tree.

## Build configuration

Binutils is configured for `mipsel-linux-gnu` with NLS, GDB, simulator, and
multilib disabled. GCC is configured as a C-only, no-bootstrap, no-target-libc
cross compiler with MIPS32r2 and soft-float defaults. Only `all-gcc` and
`install-gcc` are built; target runtime libraries are neither needed nor built.

## Source-local layout

The default work root is:

```text
<project>/.work
```

The native and Distrobox compiler installations are stored beneath:

```text
.work/toolchains/gnu-mipsel-gcc-4.7.3-binutils-2.23.2-v1/<host-flavor>
```

For example:

```text
.work/toolchains/gnu-mipsel-gcc-4.7.3-binutils-2.23.2-v1/cachyos-rolling-x86_64
.work/toolchains/gnu-mipsel-gcc-4.7.3-binutils-2.23.2-v1/ubuntu-22.04-x86_64
```

Verified downloads are stored in `.work/downloads/`; the source build and its
complete log are under `.work/toolchain-build/`. This makes a project checkout
self-contained and permits direct comparison or archival of the compiler,
objects, linker products, and logs.

Version 0.4.3 detects a verified toolchain from the former location:

```text
$XDG_CACHE_HOME/postmerkos-vcoreiii/toolchains
```

or `~/.cache/postmerkos-vcoreiii/toolchains`, and copies it into `.work/` by
default. Set `TOOLCHAIN_IMPORT_LEGACY=0` to force a fresh source-local build.

## Controls

```text
POSTMERKOS_WORK_ROOT=/custom/project-work
POSTMERKOS_TOOLCHAIN_CACHE=/custom/toolchain-root
POSTMERKOS_LEGACY_TOOLCHAIN_CACHE=/old/cache
TOOLCHAIN_IMPORT_LEGACY=0
LEGACY_TOOLCHAIN_ROOT=/custom/install/root
TOOLCHAIN_JOBS=8
TOOLCHAIN_REBUILD=1
TOOLCHAIN_KEEP_BUILD=1
ALLOW_UNVERIFIED_TOOLCHAIN=1
```

`ALLOW_UNVERIFIED_TOOLCHAIN=1` bypasses version enforcement only. It does not
disable code-generation, image-layout, or protocol validators and should not be
used for release artifacts.

## Installed manifest

Each installed toolchain includes:

```text
POSTMERKOS-TOOLCHAIN-MANIFEST.txt
```

The build verifies its toolchain ID and source hashes before compiling the
loader. Firmware build manifests additionally record the executable compiler
and linker paths and their reported versions.
## Binutils documentation bootstrap

Binutils 2.23.2 can regenerate BFD Texinfo fragments during a normal build. In
v0.4.0, `MAKEINFO=true` was exported only through the environment; the older
recursive configure/make chain did not preserve that override, and a host
without `makeinfo` failed at:

```text
WARNING: `makeinfo' is missing on your system
make[3]: *** [Makefile:421: bfd.info] Error 1
```

Version 0.4.1 both installs `texinfo` and passes `MAKEINFO=true` on each make
command line. Command-line variables propagate to recursive GNU Make instances
through `MAKEFLAGS`, so the bootstrap does not depend on documentation tools or
timestamps. `bison`, `gawk`, and `m4` are also installed and checked to prevent
similar generated-source failures later in the Binutils/GCC build.

After an incomplete v0.4.0 attempt, run:

```sh
TOOLCHAIN_REBUILD=1 make all
```

This removes the incomplete per-host installation and work tree, while keeping
the verified source archives in the download cache.

