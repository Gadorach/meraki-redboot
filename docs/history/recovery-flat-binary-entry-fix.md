# Recovery flat-binary entry correction

The v0.7.0 recovery build linked the C `_start` symbol as the ELF entry, but the
raw binary was executed at its first byte. Toolchain-generated allocated MIPS
metadata such as `.reginfo` or `.MIPS.abiflags` could precede `.text`, while the
fixed-RAM loader always jumped to `0x81000000`. Hardware therefore reached
`PMOSBOOT PASS-RECOVERY-EXEC` and then produced no `PMOSREC READY 2` banner.

The corrected payload uses `entry.S` in `.text.start`, forces that section to the
first load byte, discards non-runtime MIPS metadata, initializes a dedicated
stack and `$gp`, clears BSS, and calls `recovery_main`. The link fails unless
`_start == 0x81000000`.
