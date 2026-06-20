# VCore-III UART fixed-RAM stage-1 implementation report

## Confirmed failure addressed

The prior hardware image completed early initialization and reached the UART
hook, but the flash-resident UART C engine contained calls and literal pointers
based on its link address near zero. When the wrapper placed the active loader
near the end of the 256 KiB boot region, the first C call targeted the wrong
flash address and serial output stopped.

## Implemented architecture

- Early Jaguar1/Luton26 initialization remains in the staged GCC 10 ABI-PIC
  implementation and retains its established operation order.
- The full PMOSRAM2 protocol engine is linked independently at `0x80f00000`.
- It is embedded as data in each active/fallback loader copy.
- After DDR, cache, stack, flash mapping, and boot-mode exit are complete,
  assembly computes the blob source relative to the executing loader `$gp`.
- Assembly copies the stage to RAM, writes back/invalidates D-cache, invalidates
  I-cache, and calls the fixed address with the detected SoC family in `a0`.
- On timeout or rejected transfer, stage 1 returns and normal kernel processing
  continues.

## Memory geometry

```text
0x80000000-0x80003fff  established early stack area
0x80f00000-0x80f0ffff  reserved UART stage-1 window
0x81000000-0x87efffff  accepted uploaded executable window
```

The stage-1 reservation and upload window are validated as non-overlapping.

## Code-generation properties verified

Using the supplied GCC 4.7.3 reference compiler as a real MIPS cross-check:

- `uart_stage1_entry` links exactly at `0x80f00000`;
- allocated stage-1 size is `0x10a0` (4,256 bytes);
- final stage-1 ELF has no runtime relocations or unresolved symbols;
- ordinary calls target `0x80f0xxxx`, not flash addresses;
- entry stack frame is 4,504 bytes, within the established 16 KiB stack;
- relocatable development loader size is 20,712 bytes;
- loader stage-1 blob symbols span exactly the binary's 4,256 bytes;
- active and fallback wrapper copies embed identical stage-1 bytes;
- the only transfer into the engine is `jalr` to `0x80f00000`.

Clang/LLD structural builds also passed for strict, development, and permissive
profiles. The Clang stage-1 binary is larger (`0x2634`) but remains within the
64 KiB reservation and passes the same fixed-image validator.

## Remaining hardware validation

The new architecture requires a fresh hardware boot test. Expected serial flow
is the normal final initialization message followed by `PMOSRAM READY 2 ...`.
A probe timeout must then return to normal kernel validation and loading.
