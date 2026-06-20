# VCore-III LinuxLoader v0.5.1 build fix

## Reported failure

GNU assembler 2.23.2 rejected two instructions in the new fixed-RAM stage shim:

```text
Error: absolute expression required `li $4,loader_uart_stage1_copy_text'
Error: absolute expression required `li $4,loader_uart_stage1_return_text'
```

`li` is a pseudoinstruction for loading an assembly-time constant. The two
operands are relocatable labels whose final offsets are assigned by the linker,
so they cannot be used as absolute constants at assembly time.

## Correction

Each label address is now formed in the same runtime-base-relative way as the
other flash-loader data references:

```asm
lui   a0, %hi(label)
addiu a0, a0, %lo(label)
add   a0, a0, gp
```

The object therefore carries paired `R_MIPS_HI16`/`R_MIPS_LO16` relocations.
The final linker resolves them to loader-relative offsets, and adding runtime
`gp` selects the active or fallback loader copy.

## Exact legacy-toolchain validation

The supplied GCC 4.7.3/binutils 2.23.2 toolchain was used for a clean complete
build. Results:

- UART stage 1 linked at `0xa7f00000`; size `0x1050`.
- Fixed-stage direct-call validation passed.
- Pre-DDR initialization remained stackless.
- Flash UART control flow remained assembly-only.
- Loader size: `19048` bytes.
- Final boot-region image: `262144` bytes.
- Active loader: `0x36b30-0x3b597`.
- Fallback loader: `0x3b598-0x3ffff`.
- Both Jaguar1 and Luton26 recovery payloads built successfully.
- All 32 source/protocol regression tests passed.
