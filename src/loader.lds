OUTPUT_ARCH(mips)
ENTRY(__start)
SECTIONS
{
    . = 0;
    .loader : ALIGN(4)
    {
        KEEP(*(.text.__start))
        *(.text*)
        *(.rodata*)
        *(.srodata*)
        *(.data*)
        *(.sdata*)
        *(.got*)
        *(.lit4*)
        *(.lit8*)
    }
    __end = .;
    __loader_end = .;

    .bss (NOLOAD) : ALIGN(4)
    {
        *(.sbss*)
        *(.bss*)
        *(COMMON)
    }

    /DISCARD/ :
    {
        *(.reginfo)
        *(.MIPS.abiflags)
        *(.pdr)
        *(.comment)
        *(.note*)
        *(.eh_frame*)
    }
}
ASSERT(SIZEOF(.bss) == 0, "LinuxLoader must not require zero-initialized RAM before DDR setup")
