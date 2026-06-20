# UART fixed-RAM stage 1

A hardware-tested GCC 10 candidate completed early SoC initialization but
stopped at the UART hook. Binary analysis showed that the flash-resident UART
engine's first C call and string addresses referred to its link-time offsets
near zero rather than the relocated active loader copy.

Version 0.5 removes that engine from relocatable flash text. The validated
`uart-stage1.bin` is linked at `0x80f00000`, embedded as data, copied after DDR
and cache initialization, and entered at its fixed address. The flash shim is
entirely assembly and computes the blob source from the active loader `$gp`, so
both active and fallback copies use the correct embedded bytes.

The fixed program may use normal calls and the established stack. Its linker and
validator prohibit unresolved symbols, dynamic relocation state, BSS, an entry
other than the configured address, excessive size, or overlap with the upload
window.
