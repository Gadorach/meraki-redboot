# GNU GCC 10 loader code-generation correction

The Ubuntu 22.04 MIPS cross compiler rejects the older loader flag mixture of
`-mno-abicalls` and `-fPIC`. That mixture was inherited from combining Linux
architecture flags with the original loader-local flags.

The maintained build now states the runtime contract directly:

- no SVR4 ABI calls or GOT;
- direct calls remain within the loader's 256 MiB jump region;
- C data uses `R_MIPS_GPREL16` relative to the loader base;
- link-time `_gp` is absolute zero, matching the runtime base loaded by
  `head.S`;
- pre-DDR initialization treats saved integer registers as scratch so GCC does
  not create a stack frame before RAM exists;
- a build-time validator rejects stack references, unsupported C text
  relocations, dynamic/GOT sections, and a non-zero `_gp`.

The UART RAM-loader module runs only after DRAM and the stack are established,
so it retains the normal o32 saved-register convention while using the same
custom-GP data/call model.
