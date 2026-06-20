OUTPUT_ARCH(mips)
ENTRY(_start)
SECTIONS
{
    . = 0x81000000;
    .text : ALIGN(4)
    {
        KEEP(*(.text.start))
        *(.text*)
    }
    .rodata : ALIGN(4) { *(.rodata*) }
    .data : ALIGN(4) { *(.data*) *(.sdata*) *(.lit4*) *(.lit8*) }
    __image_end = .;
    .bss (NOLOAD) : ALIGN(4) { *(.sbss*) *(.bss*) *(COMMON) }
    /DISCARD/ : { *(.reginfo) *(.MIPS.abiflags) *(.pdr) *(.comment) *(.note*) *(.eh_frame*) *(.got*) }
}
ASSERT(_start == 0x81000000, "UART smoke-test entry must be exactly 0x81000000")
ASSERT(SIZEOF(.bss) == 0, "UART smoke test must not require BSS initialization")
