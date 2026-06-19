OUTPUT_ARCH(mips)
ENTRY(_start)
SECTIONS { . = 0x81000000; .text : { *(.text*) *(.rodata*) } .data : { *(.data*) } .bss : { *(.bss*) *(COMMON) } }
