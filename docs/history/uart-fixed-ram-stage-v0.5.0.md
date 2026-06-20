# UART fixed-RAM stage transition — v0.5.0

The first v0.4.3 hardware boot completed all low-level Jaguar1 initialization
but stopped after boot-mode exit. Binary analysis found that the flash-resident
UART C engine was not actually relocatable: direct MIPS calls and literal
addresses remained based on the link-at-zero image even though the wrapper ran
the active loader from a nonzero flash offset.

Version 0.5.0 retained the GCC 4.7.3 reset-time initialization implementation as
the early-hardware behavior reference but removed complex C from the relocatable
flash path. The UART receiver is now linked at `0xa7f00000`, embedded as data,
copied after DDR/stack initialization, and entered through an assembly `jalr`.
The user-upload range ends at `0x87f00000`, reserving the corresponding physical
top 1 MiB for the receiver's uncached alias.

This history entry records the reason for the architectural split. Current build
and use instructions are in `README.md`, `docs/BUILDING.md`, and
`docs/UART-RAMLOADER.md`.
