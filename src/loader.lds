OUTPUT_ARCH(mips)
ENTRY(__start)
SECTIONS
{
    . = 0;
    .loader : ALIGN(4)
    {
        KEEP(*(.text.__start))
        *(.text*)
        /* The fixed-RAM stage is embedded as inert bytes, never addressed by C. */
        *(.rodata.uart_stage1)
    }
    __end = .;
    __loader_end = .;

    .forbidden_loader_data : ALIGN(4)
    {
        *(.rodata*)
        *(.data*)
        *(.sdata*)
        *(.lit4*) *(.lit8*)
        *(.got) *(.got.plt)
    }
    .bss (NOLOAD) : ALIGN(4) { *(.sbss*) *(.bss*) *(COMMON) }
    /DISCARD/ : { *(.reginfo) *(.MIPS.abiflags) *(.pdr) *(.comment) *(.note*) *(.eh_frame*) }
}
ASSERT(SIZEOF(.forbidden_loader_data) == 0,
       "relocatable flash loader C must not allocate data, literals, or a GOT")
ASSERT(SIZEOF(.bss) == 0,
       "LinuxLoader must not require zero-initialized RAM before DDR setup")
