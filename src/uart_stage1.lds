OUTPUT_ARCH(mips)
ENTRY(uart_stage1_entry)
SECTIONS
{
    . = UART_STAGE1_LOAD_ADDR;
    .uart_stage1 : ALIGN(4)
    {
        KEEP(*(.text.uart_stage1_entry))
        *(.text*)
        *(.rodata*)
        *(.srodata*)
        *(.data*)
        *(.sdata*)
        *(.lit4*)
        *(.lit8*)
    }
    __uart_stage1_end = .;

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
        *(.got*)
    }
}
ASSERT(SIZEOF(.bss) == 0, "UART stage1 must not require a separately zeroed BSS")
ASSERT(__uart_stage1_end <= UART_STAGE1_LOAD_ADDR + UART_STAGE1_MAX_SIZE,
       "UART stage1 exceeds its reserved top-of-RAM window")
