# GCC 10 code-generation contract

The authoritative standalone build targets Ubuntu 22.04's little-endian MIPS
GNU toolchain (`mipsel-linux-gnu-gcc` 10.x and matching binutils). The loader
has two deliberately different execution models.

## Relocatable pre-DDR flash code

Before DDR is usable, `head.S` probes Jaguar1 first and Luton26 second. Each SoC
path is split into leaf stages for chip probing, console/GPIO setup, PLL, SPI,
DDR-controller configuration, DDR readiness, DDR training, interrupts, cache
preparation, individual cache initialization, cache enablement, PI, board state,
and completion. Assembly preserves this exact order.

The GCC 10 source contract is:

- `-mno-abicalls -fno-pic -fno-pie -G 0`;
- private helpers are `always_inline` into their owning stage;
- each stage translation unit allocates code only—no strings, tables, writable
  data, BSS, literals, or GOT;
- each exported stage is leaf-only and may not use `$sp`;
- no C stage may contain `j`, `jal`, or `jalr`;
- no C stage may retain text relocations or unresolved symbols;
- `s0` through `s7` are available as call-clobbered scratch registers, while
  the architectural frame pointer remains untouched;
- assembly forms `runtime loader base + link-time stage offset` and enters the
  result with `jalr`, preserving relocation across active and fallback copies.

GCC can emit J-format jumps for complex local state machines even when no C
function call exists. `scripts/normalize_mips_local_jumps.py` operates on the
compiler-generated assembly and changes only compiler-local targets (`$L*`,
`$BB*`, and `.L*`) from `j` to PC-relative `b`. It never rewrites function
returns or symbol calls. The final object validator rejects any J-format jump
that remains.

The clock-frequency selector is immediate-only, and serial diagnostics live in
assembly, so no pre-DDR C data address is required. DDR setup, wait, and
training are separate stages but retain the original register programming and
order.

## Fixed-RAM stage 1

The relocatable flash image embeds `uart-stage1.bin` as inert bytes. After DDR,
caches, the early stack, flash mapping, and boot-mode exit are complete,
`head.S`:

1. finds the blob relative to the active loader base;
2. copies its padded image to the hardware-proven uncached KSEG1 address
   `0xa7f00000`;
3. writes back/invalidates D-cache lines;
4. invalidates the matching I-cache lines;
5. passes the detected SoC family and runtime loader base; and
6. tail-jumps to `uart_stage1_entry`.

Stage 1 executes uncached and never returns to the relocatable flash code. It performs the UART probe
and then owns the complete boot continuation:

- PMOSRAM2 header/frame validation and optional uploaded-program execution;
- normal SPIM header discovery from the runtime loader region;
- mandatory magic, size, alignment, load-address, overflow, stack-overlap, and
  stage-1-overlap checks;
- legacy and hard size policies;
- CRC strict/warn/off policies;
- flash-to-RAM kernel copy;
- data/instruction cache maintenance;
- zeroed kernel arguments and kernel entry;
- fallback-region transfer on invalid flash payload or a returned kernel.

An uploaded diagnostic payload may return. Stage 1 prints `PMOSRAM RETURNED`
and continues with the normal flash-kernel path rather than returning to flash
loader code.

Stage 1 is fixed-address freestanding C. Ordinary local calls, strings, and its
established DRAM stack are valid, but the linked image may contain no unresolved
symbols, runtime relocations, dynamic state, GOT, or BSS. Its flat binary is
explicitly padded to a four-byte boundary before embedding because the flash
copy shim transfers words.

## Build-time enforcement

`scripts/validate_loader_codegen.py` rejects:

- any pre-DDR `$sp` use;
- a stage-entry set different from the declared Jaguar/Luton lists;
- unresolved symbols or text relocations;
- pre-DDR `j`, `jal`, or `jalr` instructions;
- allocated data, literals, BSS, or GOT sections;
- final dynamic-loader, PLT, interpreter, or relocation state.

`scripts/validate_fixed_payload.py` verifies the fixed stage-1 entry, allocated
extent, reserved range, unresolved symbols, runtime relocations, dynamic/GOT
sections, and BSS. `validate_image.py` proves that both relocatable loader copies
embed the exact validated and padded stage-1 binary.

Source-contract tests preserve the common initialization order and critical
Jaguar1/Luton26 values. The supplied GCC 4.7.3 project is used as an additional
code-generation reference, but the GCC 10 Distrobox build and hardware serial
capture remain the authoritative acceptance tests.
