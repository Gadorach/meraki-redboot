# Source policy and GCC 10 adaptation

LinuxLoader behavior is selected at compile time. Generated images are never
modified by post-link binary patching.

## Validation policies

Every profile requires a non-zero payload and enforces the
hard slot boundary. CRC and legacy-size behavior are independently selected:

| Profile | CRC | Legacy threshold | Hard boundary |
|---|---|---|---|
| `strict` | reject | reject | reject |
| `development` | warn | warn | reject |
| `permissive` | omitted | omitted | reject |

`patches/0001-vcoreiii-loader-validation-policies.patch` implements those
source-level policy branches against the imported GPL loader.

## GCC 10 pre-DDR adaptation

`patches/0002-vcoreiii-loader-gcc10-codegen.patch` is applied after GPL import.
It preserves hardware values and operation order while changing compiler-facing
structure:

- splits Jaguar1 and Luton26 initialization into explicit leaf stages;
- separates DDR configuration, readiness wait, and training;
- forces private helpers inline;
- replaces callable CP0/TLB helpers with exact statement macros;
- removes pre-DDR strings, tables, literals, GOT, BSS, and writable data;
- replaces the clock lookup table with immediate selection;
- keeps serial progress messages in assembly;
- adds runtime-base-relative assembly dispatch to each stage;
- adds the post-DDR fixed-RAM stage-1 copy/cache/tail-jump path.

The Makefile compiles pre-DDR C to assembly, then
`scripts/normalize_mips_local_jumps.py` changes only compiler-local J-format
jumps to PC-relative branches. The final validator rejects any remaining stack,
call/jump, relocation, data, GOT, BSS, or unresolved-symbol dependency.

The UART protocol and permanent flash-kernel continuation live in
`src/uart_ramloader.c`, linked separately by `src/uart_stage1.lds`; they are not
part of the GPL refresh patch.
