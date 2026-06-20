# VCore-III LinuxLoader v0.5.1 build review

## Hardware failure addressed

The v0.4.3 image completed low-level initialization but stopped immediately
after leaving boot mode because its flash-resident UART C engine used direct
MIPS call and literal addresses linked near zero. v0.5.x moves that engine to a
separately linked fixed-RAM stage at `0xa7f00000`.

## v0.5.1 correction

The initial v0.5.0 source used `li register, label` for two flash-resident serial
marker strings. GNU assembler 2.23.2 correctly rejected those labels because
they are link-time relocatable expressions. v0.5.1 uses explicit `%hi/%lo`
relocations followed by the loader runtime `gp` base.

## Validation completed

- Exact supplied GCC 4.7.3/binutils 2.23.2 complete development build passed.
- 32 Python source, validator, and UART protocol tests passed.
- Fixed-RAM stage entry: `0xa7f00000`; exact binary size: `0x1050`.
- Stage direct calls remain inside its fixed-RAM image.
- Pre-DDR Jaguar1 and Luton26 initialization remains stackless.
- Flash-resident UART control flow is assembly-only.
- Exact loader size: `19048` bytes.
- Final boot region validated at exactly `262144` bytes.
- Both recovery payload variants built successfully.

## Remaining test

Hardware must now confirm the expected serial sequence:

```text
Low level initialization complete, exiting boot mode
PMOSRAM STAGE1 COPY
PMOSRAM READY 2 SOC=jaguar1 ...
PMOSRAM STAGE1 RETURN
```

The `RETURN` line appears after the probe timeout when no upload is started.
