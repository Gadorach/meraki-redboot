OUTPUT_ARCH(mips)
ENTRY(uart_stage1_entry)

/* Override with --defsym=UART_STAGE1_ADDRESS=<address> when required. */
PROVIDE(UART_STAGE1_ADDRESS = 0xa7f00000);

SECTIONS
{
    . = UART_STAGE1_ADDRESS;
    __uart_stage1_start = .;

    /* Only this section is executable.  Validators disassemble this section
     * and never interpret the embedded recovery binaries as MIPS opcodes. */
    .text : ALIGN(16)
    {
        KEEP(*(.text.entry))
        *(.text .text.*)
    }

    .rodata : ALIGN(16)
    {
        *(.rodata .rodata.*)
        *(.srodata .srodata.*)
        *(.lit4 .lit4.*)
        *(.lit8 .lit8.*)
    }

    .data : ALIGN(16)
    {
        *(.data .data.*)
        *(.sdata .sdata.*)
    }

    __uart_stage1_code_end = .;

    /* Target-specific recovery images are carried with stage 1 but are data.
     * They are copied to 0x81000000 before execution. */
    .embedded_recovery : ALIGN(16)
    {
        __embedded_recovery_start = .;
        KEEP(*(.embedded_recovery))
        __embedded_recovery_end = .;
    }

    __uart_stage1_end = .;

    .bss (NOLOAD) : ALIGN(16)
    {
        *(.sbss .sbss.*)
        *(.bss .bss.*)
        *(COMMON)
    }

    /DISCARD/ :
    {
        *(.reginfo)
        *(.MIPS.abiflags)
        *(.pdr)
        *(.comment)
        *(.note*)
        *(.gnu.attributes)
        *(.eh_frame*)
        *(.gcc_except_table*)
        *(.got .got.*)
        *(.plt .plt.*)
        *(.dynamic)
        *(.dynsym)
        *(.rel.dyn)
        *(.rela.dyn)
    }
}

ASSERT(uart_stage1_entry == UART_STAGE1_ADDRESS,
       "UART stage-1 entry must equal its configured RAM address")
ASSERT(SIZEOF(.bss) == 0,
       "UART stage-1 must not rely on an uninitialized BSS")
ASSERT(SIZEOF(.embedded_recovery) > 0,
       "UART stage-1 must contain embedded recovery payloads")
