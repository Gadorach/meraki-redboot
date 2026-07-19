/* Embedded platform-specific firmware recovery programs for fixed-RAM stage 1. */
#ifndef RECOVERY_LUTON26_FILE
#error RECOVERY_LUTON26_FILE must name the Luton26 recovery binary
#endif
#ifndef RECOVERY_JAGUAR1_FILE
#error RECOVERY_JAGUAR1_FILE must name the Jaguar1 recovery binary
#endif
#ifndef LIVEBOOT_LUTON26_FILE
#error LIVEBOOT_LUTON26_FILE must name the Luton26 PMOSLIVE binary
#endif
#ifndef LIVEBOOT_JAGUAR1_FILE
#error LIVEBOOT_JAGUAR1_FILE must name the Jaguar1 PMOSLIVE binary
#endif

    .section .uart_recovery, "a", @progbits
    .balign 4
    .global recovery_luton26_blob_start
    .global recovery_luton26_blob_end
recovery_luton26_blob_start:
    .incbin RECOVERY_LUTON26_FILE
recovery_luton26_blob_end:
    .balign 4

    .global recovery_jaguar1_blob_start
    .global recovery_jaguar1_blob_end
recovery_jaguar1_blob_start:
    .incbin RECOVERY_JAGUAR1_FILE
recovery_jaguar1_blob_end:
    .balign 4

    .global liveboot_luton26_blob_start
    .global liveboot_luton26_blob_end
liveboot_luton26_blob_start:
    .incbin LIVEBOOT_LUTON26_FILE
liveboot_luton26_blob_end:
    .balign 4

    .global liveboot_jaguar1_blob_start
    .global liveboot_jaguar1_blob_end
liveboot_jaguar1_blob_start:
    .incbin LIVEBOOT_JAGUAR1_FILE
liveboot_jaguar1_blob_end:
    .balign 4
