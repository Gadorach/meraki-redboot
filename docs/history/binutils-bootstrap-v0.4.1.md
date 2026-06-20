# Binutils 2.23.2 bootstrap fix

## Failure identified

The source archive downloads and Binutils configuration completed correctly.
The first fatal error was not a C compiler failure: the recursive BFD build
attempted to regenerate `bfd.info`, invoked the Binutils `missing makeinfo`
wrapper, and stopped because `makeinfo` was unavailable. Earlier warnings in
`libiberty` were non-fatal.

## Root cause

Version 0.4.0 exported `MAKEINFO=true` before the top-level configure/build.
Binutils 2.23.2 creates recursive configure and make layers for BFD. That
environment-only override was not retained by the generated BFD makefile, so
its documentation target used the source-tree `missing` wrapper instead.

## Fix

- Install and require `texinfo`/`makeinfo`.
- Install and require `bison`, `gawk`, and `m4` for the legacy generated-source
  build chain.
- Pass `MAKEINFO=true` directly on every Binutils and GCC make/install command.
  GNU Make propagates command-line variables to recursive builds through
  `MAKEFLAGS`.
- Preserve the pinned GCC 4.7.3 and Binutils 2.23.2 source versions and hashes.

## Retry

```sh
TOOLCHAIN_REBUILD=1 make all
```

The downloaded source archives are retained and reverified.
