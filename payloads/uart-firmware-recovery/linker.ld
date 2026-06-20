OUTPUT_ARCH(mips)
ENTRY(_start)

SECTIONS
{
    . = 0x81000000;
    .text : ALIGN(4)
    {
        KEEP(*(.text.start))
        *(.text .text.*)
        *(.rodata .rodata.*)
    }
    .data : ALIGN(4) { *(.data .data.*) }
    .bss (NOLOAD) : ALIGN(4) { *(.bss .bss.*) *(COMMON) }

    /DISCARD/ :
    {
        *(.reginfo)
        *(.MIPS.abiflags)
        *(.pdr)
        *(.comment)
        *(.note*)
        *(.gnu.attributes)
        *(.got .got.*)
    }
}

ASSERT(_start == 0x81000000, "recovery payload entry must equal its load address")
ASSERT(SIZEOF(.bss) == 0, "recovery payload must not rely on an uninitialized BSS")
