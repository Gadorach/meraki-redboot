# VCore-III GCC 10 permanent stage-1 implementation report

Version: 0.6.0
Date: 2026-06-19

## Hardware evidence used

The supplied GCC 4.7.3 v0.5.1 reference image boots through Jaguar1 early
initialization, DDR training, cache setup, boot-mode exit, fixed-RAM stage copy,
and the three-second PMOSRAM probe. Both the quiet and malformed-header paths
reach `PMOSRAM STAGE1 RETURN`, after which the older flash continuation hangs.

That establishes two useful facts:

1. the original early hardware initialization and UART parameters are viable;
2. the uncached top-of-RAM stage address `0xa7f00000` works on the target.

v0.6.0 uses those facts but removes the failing return transition entirely.

## Permanent continuation architecture

The relocatable flash loader retains only reset-time and pre-DDR work plus a
small assembly handoff. After DDR, caches, stack setup, flash mapping, and
boot-mode exit, it:

1. prints `PMOSRAM STAGE1 COPY`;
2. finds the embedded stage image relative to the active/fallback loader base;
3. byte-copies it to uncached KSEG1 address `0xa7f00000`;
4. executes `sync`/`ehb`;
5. passes the detected SoC family and runtime loader base; and
6. tail-jumps to `uart_stage1_entry`.

There is no stage-1 return address into flash. Fixed stage 1 performs the UART
probe and then the complete normal boot continuation itself.

## Kernel continuation retained in stage 1

Stage 1 reproduces the prior loader policy and geometry:

- SPIM header at the next 256 KiB loader boundary;
- magic, non-zero size, 32-byte alignment, hard slot limit, and load-address
  checks;
- legacy strict/warn/hard-only size behavior;
- CRC strict/warn/off behavior using the same zeroed CRC field definition;
- physical overlap rejection for fixed stage 1 and the early stack;
- flash-to-RAM word copy;
- D-cache writeback/invalidation and I-cache invalidation;
- zeroed `a0` through `a3` before kernel entry;
- fallback-region transfer with the existing reason convention.

An uploaded PMOSRAM executable may return. Stage 1 prints `PMOSRAM RETURNED`
and proceeds into the normal flash-kernel path without touching relocatable
flash code again.

## GCC 10 pre-DDR model

Pre-DDR Jaguar1 and Luton26 code is divided into explicit data-free leaf stages.
The stages use no stack, C calls, GOT, strings, tables, BSS, or text relocations.
Assembly computes each stage address as runtime-loader-base plus link offset.

GCC can emit local J-format jumps for complex state machines. The build compiles
C to assembly first and rewrites only compiler-local `$L*`, `$BB*`, and `.L*`
`j` instructions to PC-relative `b`. Returns and symbol calls are never
rewritten. The object validator rejects every stack reference, `j`, `jal`,
`jalr`, allocated data section, unresolved symbol, or remaining text
relocation.

## Fixed-RAM geometry

```text
0x80000000-0x80003fff  early stack area
0x81000000-0x87efffff  accepted cached upload interval
0xa7f00000-0xa7ffffff  uncached stage-1 reservation
```

`0xa7f00000` is the KSEG1 alias of the physical address where the cached upload
interval ends. Configuration validation enforces that relationship. Stage 1
also compares physical aliases when validating uploaded and flash-kernel load
ranges.

## Non-destructive RAM execution test

`make uart-smoke-test` builds a fixed payload at `0x81000000`. It prints
`PMOSRAM SMOKE OK` and returns. Expected sequence:

```text
PMOSRAM VERIFIED SHA256=...
PMOSRAM EXEC 81000000
PMOSRAM SMOKE OK
PMOSRAM RETURNED
PMOSRAM FLASH-BOOT LOAD=... SIZE=... ENTRY=...
```

This test performs no SPI erase or program operation.

## Validation completed

- 28 Python source, policy, protocol, and smoke-payload tests passed.
- Positive/negative code-generation fixtures passed.
- Clang/LLD structural builds passed for strict, development, and permissive.
- Luton26 and Jaguar1 recovery payload structural builds passed.
- GCC 4.7.3/binutils 2.23.2 reference cross-build passed for all profiles:
  - strict loader: 12,736 bytes;
  - development loader: 18,400 bytes;
  - permissive loader: 9,600 bytes;
  - development fixed stage 1: 5,328 bytes at `0xa7f00000`.
- Every wrapper image is exactly 262,144 bytes.
- The fixed stage has no unresolved symbols or runtime relocations.
- The source-refresh GCC 10 patch applies and reproduces all four imported
  source files byte-for-byte.
- The 132-byte GCC 4.7.3 smoke payload validates at `0x81000000`.

## Remaining acceptance

The exact Ubuntu GCC 10.3/binutils 2.38 build must be run in the user's
Distrobox. The resulting image then requires hardware confirmation of:

1. quiet-probe transition to `PMOSRAM FLASH-BOOT` and kernel boot;
2. malformed-header abort followed by the same flash boot;
3. smoke-payload upload, execution, return, and kernel boot;
4. fallback-region behavior.

The destructive recovery payload remains a separate later acceptance stage.
