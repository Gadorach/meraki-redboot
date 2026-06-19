# How the loader was separated from Linux

The original Kbuild target has only three object inputs:

```text
head.o
init_luton26.o
init_jaguar.o
```

The C files use Vitesse register headers plus a small set of Linux/MIPS types and
CP0/cache/UART macros. They do not call the Linux runtime, allocator, scheduler,
libc, or kernel services. This made a freestanding extraction practical.

The standalone tree vendors:

- the four original loader source/header files;
- the transitive Vitesse register headers used by those files;
- minimal definitions for the exact Linux/MIPS macros referenced;
- a local linker script that collects text, read-only data, PIC GOT/data, and
  asserts that no pre-DDR BSS dependency is introduced.

No Linux kernel object is linked. The custom compatibility headers should stay
minimal: when future source changes introduce a new Linux macro, add and review
only that definition rather than copying broad kernel subsystems into the tool.

`make refresh-source` is the repeatable extraction path. It deliberately does
not overwrite `src/loader.lds`, `src/boot_wrapper.S`, `src/wrapper.lds`, or the
compatibility headers because those are the standalone implementation.
