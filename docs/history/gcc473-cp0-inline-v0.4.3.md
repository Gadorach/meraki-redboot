# GCC 4.7.3 pre-DDR compatibility-helper correction

## Reported failure

The source-built GCC 4.7.3 toolchain completed successfully, but validation
rejected `init_luton26.o` because it contained a 32-byte O32 stack frame before
DDR was available. The prologue saved `ra` and `s8` and spilled a temporary.

## Exact root cause

The reset-time helpers in `src/init.h` had already been converted to
`always_inline`, but two compatibility helpers remained ordinary `static
inline` functions in `include/asm/mipsregs.h`:

- `mtc0_tlbw_hazard()`
- `tlb_write_indexed()`

Both are used by the TLB initialization path near the beginning of
`init_system_luton26()`. Under GCC 4.7.3 with `-Os`, plain `inline` is a request,
not a requirement. GCC retained real calls to these wrappers. Those calls
required preserving the incoming return address and live values, which created
the rejected stack frame. GCC documents `always_inline` as the attribute that
forces an inline function to be expanded at its call site.

## Correction

- Both CP0/TLB helpers are now declared `static inline
  __attribute__((always_inline))`.
- Their assembly bodies remain unchanged.
- The pre-DDR stack and call/link validator remains fail-closed.
- Each object is accompanied by generated assembly, disassembly, and relocation
  output before validation runs.
- The validator now reports stack and call offenders together.

## Source-local diagnostic tree

All generated components now live below `.work/` in the source tree, including:

- pinned GCC/binutils;
- compiler source-build log;
- loader and recovery objects/ELFs;
- generated `.s`, `.dis`, and `.relocs` files;
- complete build and validation logs;
- final boot-region and recovery artifacts.

A prior verified compiler under `~/.cache/postmerkos-vcoreiii/toolchains` is
copied into `.work/` automatically. Run `make support-bundle` after any build
failure to create a compact archive suitable for comparison.

## Rebuild

```sh
make clean
make all
```

The compiler does not need to be rebuilt. If another validation error occurs,
run:

```sh
make support-bundle
```

and share the archive from `.work/support/`.
