/* Fixed-RAM entry veneer and non-returning jump helpers. */
#include <asm/regdef.h>

    .set noreorder
    .section .text.uart_stage1_entry, "ax", @progbits
    .global uart_stage1_entry
    .ent uart_stage1_entry
uart_stage1_entry:
    b uart_stage1_main
     nop
    .end uart_stage1_entry

    .global uart_stage1_jump_kernel
    .ent uart_stage1_jump_kernel
uart_stage1_jump_kernel:
    move t9, a0
    move a0, zero
    move a1, zero
    move a2, zero
    jr t9
     move a3, zero
    .end uart_stage1_jump_kernel

    .global uart_stage1_jump_fallback
    .ent uart_stage1_jump_fallback
uart_stage1_jump_fallback:
    move t9, a0
    move k0, a1
    jr t9
     nop
    .end uart_stage1_jump_fallback
