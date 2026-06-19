/*
 * Source-owned 256 KiB VCore-III reset-vector wrapper.
 *
 * The vector/trampoline instruction sequence is reconstructed from the two
 * GPL-adjacent Meraki images. Both embedded loader copies are generated from
 * src/head.S, src/init_*.c and the Vitesse headers in this same build.
 */
#include <asm/regdef.h>

#ifndef ACTIVE_OFFSET
#error ACTIVE_OFFSET must be supplied by the build
#endif
#ifndef FALLBACK_OFFSET
#error FALLBACK_OFFSET must be supplied by the build
#endif
#ifndef LOADER_LENGTH
#error LOADER_LENGTH must be supplied by the build
#endif
#ifndef LOADER_FILE
#error LOADER_FILE must be supplied by the build
#endif

#define HI16(x) (((x) >> 16) & 0xffff)
#define LO16(x) ((x) & 0xffff)

    .set noreorder
    .section .boot, "ax", @progbits
    .global vector_reset

    .org 0x000, 0xff
vector_reset:
    xor  k0, k0, k0
    ori  k0, k0, 0
    bal  wrapper_dispatch
     nop

    .org 0x080, 0xff
vector_tlb:
    xor  k0, k0, k0
    ori  k0, k0, 1
    bal  wrapper_dispatch
     nop

    .org 0x100, 0xff
vector_cache:
    xor  k0, k0, k0
    ori  k0, k0, 2
    bal  wrapper_dispatch
     nop

    .org 0x180, 0xff
vector_exception:
    xor  k0, k0, k0
    ori  k0, k0, 3
    bal  wrapper_dispatch
     nop

    .org 0x200, 0xff
vector_interrupt:
    xor  k0, k0, k0
    ori  k0, k0, 4
    bal  wrapper_dispatch
     nop

    .org 0x400, 0xff
wrapper_dispatch:
    lui  k1, 0xffff
    ori  k1, k1, 0xfc00
    and  ra, k1, ra
    b    1f
1:   nop

    b    active_selector
     nop

    .org 0x41c, 0xff
fallback_descriptor:
    lui  k1, HI16(FALLBACK_OFFSET)
    ori  k1, k1, LO16(FALLBACK_OFFSET)
    add  k1, ra, k1
    jr   k1
     nop
    .word LOADER_LENGTH

    .org 0x434, 0xff
active_selector:
    b    active_descriptor
     nop
    b    wrapper_descriptor_end
     nop

    .org 0x444, 0xff
active_descriptor:
    lui  k1, HI16(ACTIVE_OFFSET)
    ori  k1, k1, LO16(ACTIVE_OFFSET)
    add  k1, ra, k1
    jr   k1
     nop
    .word LOADER_LENGTH

    .org 0x45c, 0xff
wrapper_descriptor_end:
    .org ACTIVE_OFFSET, 0xff
active_loader:
    .incbin LOADER_FILE

    .org FALLBACK_OFFSET, 0xff
fallback_loader:
    .incbin LOADER_FILE

    .org 0x40000, 0xff
