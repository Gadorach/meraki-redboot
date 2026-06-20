/* Embedded target-specific firmware-recovery payloads for fixed-RAM stage 1.
 *
 * These bytes are data, not executable stage-1 code.  uart_ramloader.c copies
 * the selected payload to its fixed 0x81000000 link address before entering it.
 */
#ifndef UART_RECOVERY_LUTON26_FILE
#error UART_RECOVERY_LUTON26_FILE is required
#endif
#ifndef UART_RECOVERY_JAGUAR1_FILE
#error UART_RECOVERY_JAGUAR1_FILE is required
#endif

    .section .embedded_recovery,"a",@progbits
    .balign 16
    .global uart_recovery_luton26_start
    .global uart_recovery_luton26_end
uart_recovery_luton26_start:
    .incbin UART_RECOVERY_LUTON26_FILE
uart_recovery_luton26_end:
    .balign 16

    .global uart_recovery_jaguar1_start
    .global uart_recovery_jaguar1_end
uart_recovery_jaguar1_start:
    .incbin UART_RECOVERY_JAGUAR1_FILE
uart_recovery_jaguar1_end:
    .balign 16
