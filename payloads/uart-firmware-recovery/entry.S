/* Flat-binary entry veneer for the pre-kernel recovery payload. */
    .set noreorder
    .section .text.start, "ax", @progbits
    .global _start
    .ent _start
_start:
    /* Keep the recovery stack below the 0x81400000 firmware staging area. */
    lui     $sp, 0x813f
    ori     $sp, $sp, 0xf000

    /* Initialise gp even when a newer compiler emits small-data references. */
    lui     $gp, %hi(_gp)
    addiu   $gp, $gp, %lo(_gp)

    /* Flat objcopy images do not contain NOBITS data; clear .bss explicitly. */
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
    jal     recovery_main
     nop
3:
    wait
    b       3b
     nop
    .end _start
