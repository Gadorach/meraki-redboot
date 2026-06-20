/* Fixed-RAM entry veneer for the UART RAM-loader engine. */
#include <asm/regdef.h>

    .set noreorder
    .section .text.uart_stage1_entry, "ax", @progbits
    .global uart_stage1_entry
    .ent uart_stage1_entry
uart_stage1_entry:
    b uart_ramloader_probe_and_run
     nop
    .end uart_stage1_entry
