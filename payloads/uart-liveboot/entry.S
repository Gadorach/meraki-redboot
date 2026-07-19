/* Flat-binary entry veneer for the high-RAM PMOSLIVE payload. */
    .set noreorder
    .section .text.start, "ax", @progbits
    .global _start
    .ent _start
_start:
    /* PMOSLIVE occupies 0x86c00000-0x86ffffff. */
    lui     $sp, 0x86ff
    ori     $sp, $sp, 0xf000

    lui     $gp, %hi(_gp)
    addiu   $gp, $gp, %lo(_gp)

    lui     $t0, %hi(__bss_start)
    addiu   $t0, $t0, %lo(__bss_start)
    lui     $t1, %hi(__bss_end)
    addiu   $t1, $t1, %lo(__bss_end)
1:
    beq     $t0, $t1, 2f
     nop
    sw      $zero, 0($t0)
    b       1b
     addiu  $t0, $t0, 4
2:
    jal     liveboot_main
     nop
3:
    wait
    b       3b
     nop
    .end _start

    .global liveboot_jump_linux
    .ent liveboot_jump_linux
liveboot_jump_linux:
    move    $t9, $a0
    move    $a0, $a1
    move    $a1, $a2
    move    $a2, $a3
    lw      $a3, 16($sp)
    jr      $t9
     nop
    .end liveboot_jump_linux
