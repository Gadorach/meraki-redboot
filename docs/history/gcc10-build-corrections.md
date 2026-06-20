# GNU MIPS GCC 10 build corrections

The source originally combined no-ABI MIPS compilation with PIC generation,
which GNU GCC 10 rejects. The current loader instead uses direct calls and
GP-relative small data with link-time `_gp = 0`, matching the runtime base set
by `head.S`.

Pre-DDR initialization cannot use a stack. GCC may otherwise preserve o32
callee-saved registers, so `s0` through `s7` are compiled as call-clobbered for
the two initialization translation units. The architectural frame-pointer
register is intentionally not included: GCC rejects `-fcall-used-fp` and its
`s8` alias. A mandatory validator disassembles both objects and rejects every
stack-pointer reference, while also rejecting ABI/GOT relocations and dynamic
sections.
