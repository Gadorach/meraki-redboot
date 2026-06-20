/*
 * Bench UART RAM loader for VCore-III LinuxLoader.
 *
 * Protocol v2 is acknowledged and chunked. Every receive wait is bounded, each
 * chunk has CRC-32, and the complete executable is checked with CRC-32 and
 * SHA-256 before cache maintenance and execution.
 */

/* This engine is copied from flash into fixed RAM before it runs.  It is not
 * part of the relocatable flash text, so ordinary GCC 10 calls, strings, and
 * stack frames are valid.  Helpers remain inline candidates, but are no longer
 * required to inline merely to make the flash image relocatable. */
#ifndef UART_STAGE1_INLINE
#define UART_STAGE1_INLINE static inline
#endif
#ifndef UART_STAGE1_CONST_DATA
#define UART_STAGE1_CONST_DATA
#endif
#define PMOS_ALWAYS_INLINE UART_STAGE1_INLINE
#define PMOS_CONST_DATA UART_STAGE1_CONST_DATA
#define UART_STAGE1_ENTRY __attribute__((section(".text.entry"), used, noinline, noreturn))
#include <postmerkos_uart_crypto.h>

typedef pmos_u8 u8;
typedef pmos_u32 u32;
#define UART_PUTS_LITERAL(value) do { \
    static const char loader_text[] UART_STAGE1_CONST_DATA = value; \
    uart_puts(loader_text); \
} while (0)
#define RETURN_ABORT_LITERAL(value) do { \
    static const char abort_reason[] UART_STAGE1_CONST_DATA = value; \
    return abort_to_boot(abort_reason); \
} while (0)
#define SEND_STATUS_LITERAL(value, sequence_value) do { \
    static const char status_prefix[] UART_STAGE1_CONST_DATA = value; \
    send_sequence_status(status_prefix, sequence_value); \
} while (0)

#define UART_BASE 0x70100000u
#define UART_RBR  0u
#define UART_THR  0u
#define UART_LSR  20u
#define UART_LSR_DR   0x01u
#define UART_LSR_THRE 0x20u

#define RAMLOADER_MAGIC0 0x534f4d50u /* PMOS */
#define RAMLOADER_MAGIC1 0x324d4152u /* RAM2 */
#define RAMLOADER_FRAME_MAGIC 0x32464d52u /* RMF2 */
#define RAMLOADER_PROTOCOL_VERSION 2u
#define RAMLOADER_HEADER_BYTES 72u
#define RAMLOADER_FRAME_HEADER_BYTES 16u
#define RAMLOADER_MAX_CHUNK 4096u
#define RAMLOADER_CACHE_LINE 32u
#define RAMLOADER_TRANSFER_TIMEOUT_MS (15u * 60u * 1000u)
#define RAMLOADER_SOC_LUTON26 1u
#define RAMLOADER_SOC_JAGUAR1 2u


#ifndef CONFIG_LOADER_REGION_SIZE
#define CONFIG_LOADER_REGION_SIZE 0x00040000u
#endif
#ifndef CONFIG_FALLBACK_REGION_SIZE
#define CONFIG_FALLBACK_REGION_SIZE 0x00400000u
#endif
#ifndef CONFIG_HARD_PAYLOAD_LIMIT
#define CONFIG_HARD_PAYLOAD_LIMIT 0x003bffe0u
#endif
#ifndef CONFIG_LEGACY_PAYLOAD_LIMIT
#define CONFIG_LEGACY_PAYLOAD_LIMIT CONFIG_HARD_PAYLOAD_LIMIT
#endif
#ifndef CONFIG_UART_STAGE1_ADDRESS
#define CONFIG_UART_STAGE1_ADDRESS 0xa7f00000u
#endif
#ifndef CONFIG_UART_STAGE1_MAX_SIZE
#define CONFIG_UART_STAGE1_MAX_SIZE 0x00100000u
#endif
#define FLASH_PAYLOAD_MAGIC 0x4d495053u
#define FLASH_PAYLOAD_HEADER_BYTES 32u
#define EARLY_STACK_START 0x80000000u
#define EARLY_STACK_END   0x80004000u
#define RECOVERY_LOAD_ADDRESS 0x81000000u
#define RECOVERY_ENTRY_ADDRESS 0x81000000u

#ifndef CONFIG_UART_RAMLOADER_MAX_SIZE
#define CONFIG_UART_RAMLOADER_MAX_SIZE 0x00400000u
#endif
#ifndef CONFIG_UART_RAMLOADER_RAM_START
#define CONFIG_UART_RAMLOADER_RAM_START 0x81000000u
#endif
#ifndef CONFIG_UART_RAMLOADER_RAM_END
#define CONFIG_UART_RAMLOADER_RAM_END 0x87f00000u
#endif
#ifndef CONFIG_UART_RAMLOADER_PROBE_TIMEOUT_MS
#define CONFIG_UART_RAMLOADER_PROBE_TIMEOUT_MS 3000u
#endif
#ifndef CONFIG_UART_RAMLOADER_INTERBYTE_TIMEOUT_MS
#define CONFIG_UART_RAMLOADER_INTERBYTE_TIMEOUT_MS 3000u
#endif
#ifndef CONFIG_UART_MENU_TIMEOUT_MS
#define CONFIG_UART_MENU_TIMEOUT_MS 5000u
#endif
#ifndef CONFIG_UART_RAMLOADER_COUNT_HZ
/* init_pll() configures a 416 MHz CPU; CP0 Count advances at half rate. */
#define CONFIG_UART_RAMLOADER_COUNT_HZ 208000000u
#endif

struct ram_header {
    u32 magic0;
    u32 magic1;
    u32 version;
    u32 flags;
    u32 load_addr;
    u32 entry_addr;
    u32 total_size;
    u32 chunk_size;
    u32 image_crc32;
    u8 image_sha256[32];
    u32 header_crc32;
};

struct frame_header {
    u32 magic;
    u32 sequence;
    u32 length;
    u32 crc32;
};

struct pmos_timer {
    u32 last_count;
    u32 remainder_ticks;
    u32 elapsed_ms;
};

extern const u8 uart_recovery_luton26_start[];
extern const u8 uart_recovery_luton26_end[];
extern const u8 uart_recovery_jaguar1_start[];
extern const u8 uart_recovery_jaguar1_end[];

static volatile u32 * const uart = (volatile u32 *)UART_BASE;

UART_STAGE1_INLINE u32 cp0_count(void)
{
    u32 value;
    __asm__ __volatile__("mfc0 %0, $9" : "=r"(value));
    return value;
}

UART_STAGE1_INLINE void timer_start(struct pmos_timer *timer)
{
    timer->last_count = cp0_count();
    timer->remainder_ticks = 0u;
    timer->elapsed_ms = 0u;
}

UART_STAGE1_INLINE int timer_expired(struct pmos_timer *timer, u32 limit_ms)
{
    const u32 ticks_per_ms = CONFIG_UART_RAMLOADER_COUNT_HZ / 1000u;
    u32 now = cp0_count();
    u32 delta = now - timer->last_count;
    u32 whole_ms = delta / ticks_per_ms;
    u32 remainder = delta % ticks_per_ms;
    timer->last_count = now;
    timer->elapsed_ms += whole_ms;
    timer->remainder_ticks += remainder;
    if (timer->remainder_ticks >= ticks_per_ms) {
        timer->elapsed_ms++;
        timer->remainder_ticks -= ticks_per_ms;
    }
    return timer->elapsed_ms >= limit_ms;
}

UART_STAGE1_INLINE int uart_rx_ready(void)
{
    return (uart[UART_LSR / 4] & UART_LSR_DR) != 0;
}

UART_STAGE1_INLINE u8 uart_getc_now(void)
{
    return (u8)(uart[UART_RBR / 4] & 0xffu);
}

/* The timeout covers the entire requested block, not each individual byte. */
UART_STAGE1_INLINE int uart_recv_exact(u8 *destination, u32 length, u32 milliseconds)
{
    struct pmos_timer timer;
    u32 received = 0u;
    timer_start(&timer);
    while (received < length) {
        while (!uart_rx_ready()) {
            if (timer_expired(&timer, milliseconds)) return 0;
        }
        destination[received++] = (u8)(uart[UART_RBR / 4] & 0xffu);
        if (timer_expired(&timer, milliseconds) && received < length) return 0;
    }
    return 1;
}

UART_STAGE1_INLINE void uart_drain_rx(void)
{
    u32 remaining = 8192u;
    while (remaining-- && uart_rx_ready()) (void)uart[UART_RBR / 4];
}

UART_STAGE1_INLINE void uart_putc(char c)
{
    if (c == '\n') {
        while ((uart[UART_LSR / 4] & UART_LSR_THRE) == 0) {}
        uart[UART_THR / 4] = (u32)(u8)'\r';
    }
    while ((uart[UART_LSR / 4] & UART_LSR_THRE) == 0) {}
    uart[UART_THR / 4] = (u32)(u8)c;
}

UART_STAGE1_INLINE void uart_puts(const char *text)
{
    while (*text) uart_putc(*text++);
}

UART_STAGE1_INLINE void uart_put_hex_nibble(u32 value)
{
    value &= 0xfu;
    uart_putc((char)(value < 10u ? ('0' + value) : ('a' + value - 10u)));
}

UART_STAGE1_INLINE void uart_put_hex32(u32 value)
{
    int shift;
    for (shift = 28; shift >= 0; shift -= 4) uart_put_hex_nibble(value >> shift);
}

UART_STAGE1_INLINE u32 get_le32(const u8 *data)
{
    return (u32)data[0] | ((u32)data[1] << 8) |
           ((u32)data[2] << 16) | ((u32)data[3] << 24);
}

UART_STAGE1_INLINE void parse_header(struct ram_header *header, const u8 *raw)
{
    u32 i;
    header->magic0 = get_le32(raw);
    header->magic1 = get_le32(raw + 4);
    header->version = get_le32(raw + 8);
    header->flags = get_le32(raw + 12);
    header->load_addr = get_le32(raw + 16);
    header->entry_addr = get_le32(raw + 20);
    header->total_size = get_le32(raw + 24);
    header->chunk_size = get_le32(raw + 28);
    header->image_crc32 = get_le32(raw + 32);
    for (i = 0; i < 32u; i++) header->image_sha256[i] = raw[36u + i];
    header->header_crc32 = get_le32(raw + 68);
}

UART_STAGE1_INLINE void parse_frame_header(struct frame_header *frame, const u8 *raw)
{
    frame->magic = get_le32(raw);
    frame->sequence = get_le32(raw + 4);
    frame->length = get_le32(raw + 8);
    frame->crc32 = get_le32(raw + 12);
}

UART_STAGE1_INLINE int ranges_overlap(u32 start_a, u32 size_a, u32 start_b, u32 size_b)
{
    u32 end_a = start_a + size_a;
    u32 end_b = start_b + size_b;
    if (end_a < start_a || end_b < start_b) return 1;
    return start_a < end_b && start_b < end_a;
}

UART_STAGE1_INLINE int ranges_overlap_physical(u32 start_a, u32 size_a,
                                                u32 start_b, u32 size_b)
{
    return ranges_overlap(start_a & 0x1fffffffu, size_a,
                          start_b & 0x1fffffffu, size_b);
}

UART_STAGE1_INLINE int validate_header(const struct ram_header *header, const u8 *raw)
{
    u32 end;
    if (header->magic0 != RAMLOADER_MAGIC0 || header->magic1 != RAMLOADER_MAGIC1) return 0;
    if (header->version != RAMLOADER_PROTOCOL_VERSION || header->flags != 0u) return 0;
    if (header->header_crc32 != pmos_crc32(raw, RAMLOADER_HEADER_BYTES - 4u)) return 0;
    if (header->load_addr < CONFIG_UART_RAMLOADER_RAM_START) return 0;
    if (header->total_size == 0u || header->total_size > CONFIG_UART_RAMLOADER_MAX_SIZE) return 0;
    end = header->load_addr + header->total_size;
    if (end < header->load_addr || end > CONFIG_UART_RAMLOADER_RAM_END) return 0;
    if ((header->load_addr & 3u) != 0u || (header->entry_addr & 3u) != 0u) return 0;
    if (header->entry_addr < header->load_addr || header->entry_addr >= end) return 0;
    if (header->chunk_size < 64u || header->chunk_size > RAMLOADER_MAX_CHUNK) return 0;
    if (ranges_overlap_physical(header->load_addr, header->total_size,
                                CONFIG_UART_STAGE1_ADDRESS,
                                CONFIG_UART_STAGE1_MAX_SIZE)) return 0;
    return 1;
}

UART_STAGE1_INLINE void send_sequence_status(const char *prefix, u32 sequence)
{
    uart_puts(prefix);
    uart_put_hex32(sequence);
    UART_PUTS_LITERAL("\n");
}

UART_STAGE1_INLINE void cache_prepare_for_execution(u32 start, u32 size)
{
    u32 address = start & ~(RAMLOADER_CACHE_LINE - 1u);
    u32 end = (start + size + RAMLOADER_CACHE_LINE - 1u) & ~(RAMLOADER_CACHE_LINE - 1u);
    for (; address < end; address += RAMLOADER_CACHE_LINE) {
        __asm__ __volatile__(
            ".set push\n\t"
            ".set noreorder\n\t"
            ".set mips32r2\n\t"
            "cache 0x15, 0(%0)\n\t" /* Hit writeback/invalidate D-cache */
            ".set pop\n\t" : : "r"(address) : "memory");
    }
    __asm__ __volatile__("sync" ::: "memory");
    address = start & ~(RAMLOADER_CACHE_LINE - 1u);
    for (; address < end; address += RAMLOADER_CACHE_LINE) {
        __asm__ __volatile__(
            ".set push\n\t"
            ".set noreorder\n\t"
            ".set mips32r2\n\t"
            "cache 0x10, 0(%0)\n\t" /* Hit invalidate I-cache */
            ".set pop\n\t" : : "r"(address) : "memory");
    }
    __asm__ __volatile__("sync\n\tehb" ::: "memory");
}

UART_STAGE1_INLINE int abort_to_boot(const char *reason)
{
    UART_PUTS_LITERAL("PMOSRAM ABORT ");
    uart_puts(reason);
    UART_PUTS_LITERAL("\n");
    uart_drain_rx();
    return 0;
}

static int uart_ramloader_run_session(u32 soc_family)
{
    u8 raw[RAMLOADER_HEADER_BYTES];
    u8 frame_raw[RAMLOADER_FRAME_HEADER_BYTES];
    u8 chunk[RAMLOADER_MAX_CHUNK];
    u8 digest[32];
    struct ram_header header;
    struct frame_header frame;
    pmos_sha256_ctx sha;
    u32 received, expected_sequence, image_crc;
    u32 previous_length, previous_crc, frame_crc;
    u32 i;
    struct pmos_timer transfer_timer;
    u8 *destination;
    void (*entry)(void);

    UART_PUTS_LITERAL("PMOSRAM READY 2 SOC=");
    if (soc_family == RAMLOADER_SOC_LUTON26)
        UART_PUTS_LITERAL("luton26");
    else if (soc_family == RAMLOADER_SOC_JAGUAR1)
        UART_PUTS_LITERAL("jaguar1");
    else
        UART_PUTS_LITERAL("unknown");
    UART_PUTS_LITERAL(" MAX="); uart_put_hex32(CONFIG_UART_RAMLOADER_MAX_SIZE);
    UART_PUTS_LITERAL(" RAM="); uart_put_hex32(CONFIG_UART_RAMLOADER_RAM_START);
    uart_putc('-'); uart_put_hex32(CONFIG_UART_RAMLOADER_RAM_END);
    UART_PUTS_LITERAL(" CHUNK="); uart_put_hex32(RAMLOADER_MAX_CHUNK);
    UART_PUTS_LITERAL("\n");

    if (!uart_recv_exact(raw, RAMLOADER_HEADER_BYTES, CONFIG_UART_RAMLOADER_INTERBYTE_TIMEOUT_MS))
        RETURN_ABORT_LITERAL("HEADER-TIMEOUT");
    parse_header(&header, raw);
    if (!validate_header(&header, raw)) RETURN_ABORT_LITERAL("INVALID-HEADER");

    UART_PUTS_LITERAL("PMOSRAM HEADER-ACK SIZE="); uart_put_hex32(header.total_size);
    UART_PUTS_LITERAL(" LOAD="); uart_put_hex32(header.load_addr);
    UART_PUTS_LITERAL(" ENTRY="); uart_put_hex32(header.entry_addr); UART_PUTS_LITERAL("\n");

    destination = (u8 *)header.load_addr;
    received = 0u;
    expected_sequence = 0u;
    previous_length = 0u;
    previous_crc = 0u;
    image_crc = 0xffffffffu;
    pmos_sha256_init(&sha);
    timer_start(&transfer_timer);

    while (received < header.total_size) {
        if (timer_expired(&transfer_timer, RAMLOADER_TRANSFER_TIMEOUT_MS))
            RETURN_ABORT_LITERAL("TRANSFER-TIMEOUT");
        if (!uart_recv_exact(frame_raw, RAMLOADER_FRAME_HEADER_BYTES,
                             CONFIG_UART_RAMLOADER_INTERBYTE_TIMEOUT_MS))
            RETURN_ABORT_LITERAL("FRAME-HEADER-TIMEOUT");
        parse_frame_header(&frame, frame_raw);
        if (frame.magic != RAMLOADER_FRAME_MAGIC || frame.length == 0u ||
            frame.length > header.chunk_size || frame.length > RAMLOADER_MAX_CHUNK)
            RETURN_ABORT_LITERAL("INVALID-FRAME");
        if (!uart_recv_exact(chunk, frame.length, CONFIG_UART_RAMLOADER_INTERBYTE_TIMEOUT_MS))
            RETURN_ABORT_LITERAL("FRAME-DATA-TIMEOUT");
        frame_crc = pmos_crc32(chunk, frame.length);
        if (expected_sequence != 0u && frame.sequence == expected_sequence - 1u) {
            if (frame.length == previous_length && frame.crc32 == previous_crc &&
                frame_crc == frame.crc32)
                SEND_STATUS_LITERAL("PMOSRAM ACK ", frame.sequence);
            else
                SEND_STATUS_LITERAL("PMOSRAM NACK ", expected_sequence);
            continue;
        }
        if (frame.sequence != expected_sequence || frame_crc != frame.crc32 ||
            frame.length > header.total_size - received) {
            SEND_STATUS_LITERAL("PMOSRAM NACK ", expected_sequence);
            continue;
        }
        for (i = 0; i < frame.length; i++) destination[received + i] = chunk[i];
        image_crc = pmos_crc32_update(image_crc, chunk, frame.length);
        pmos_sha256_update(&sha, chunk, frame.length);
        received += frame.length;
        previous_length = frame.length;
        previous_crc = frame.crc32;
        SEND_STATUS_LITERAL("PMOSRAM ACK ", expected_sequence);
        expected_sequence++;
    }

    image_crc = ~image_crc;
    pmos_sha256_final(&sha, digest);
    if (image_crc != header.image_crc32) RETURN_ABORT_LITERAL("IMAGE-CRC32-MISMATCH");
    if (!pmos_digest_equal(digest, header.image_sha256, 32u)) RETURN_ABORT_LITERAL("IMAGE-SHA256-MISMATCH");

    UART_PUTS_LITERAL("PMOSRAM VERIFIED SHA256=");
    for (i = 0; i < 32u; i++) {
        uart_put_hex_nibble(digest[i] >> 4);
        uart_put_hex_nibble(digest[i]);
    }
    UART_PUTS_LITERAL("\nPMOSRAM EXEC "); uart_put_hex32(header.entry_addr); UART_PUTS_LITERAL("\n");
    cache_prepare_for_execution(header.load_addr, header.total_size);
    entry = (void (*)(void))header.entry_addr;
    entry();
    UART_PUTS_LITERAL("PMOSRAM RETURNED\n");
    return 0;
}


__attribute__((noreturn, noinline)) static void jump_to_kernel(u32 target)
{
    __asm__ __volatile__(
        ".set push\n\t"
        ".set noreorder\n\t"
        "move $4, $0\n\t"
        "move $5, $0\n\t"
        "move $6, $0\n\t"
        "move $7, $0\n\t"
        "jr %0\n\t"
        "nop\n\t"
        ".set pop\n\t"
        : : "r"(target) : "memory");
    for (;;) {}
}

UART_STAGE1_INLINE int uart_wait_for_interrupt(u32 timeout_ms)
{
    struct pmos_timer timer;
    timer_start(&timer);
    while (!uart_rx_ready()) {
        if (timer_expired(&timer, timeout_ms)) return 0;
    }
    (void)uart_getc_now(); /* Interrupt byte is never interpreted as a choice. */
    return 1;
}

UART_STAGE1_INLINE void uart_drain_input(void)
{
    while (uart_rx_ready()) (void)uart_getc_now();
}

UART_STAGE1_INLINE int uart_menu_selection(u32 timeout_ms)
{
    struct pmos_timer timer;
    u8 choice;

    /* The byte that opened the menu and any already-buffered noise must never
     * double as a menu choice.  Only input received after this prompt counts. */
    uart_drain_input();
    UART_PUTS_LITERAL("PMOSMENU SELECT 1=RAM-LOADER 2=FW-RECOVERY TIMEOUT_MS=");
    uart_put_hex32(timeout_ms);
    UART_PUTS_LITERAL("\n");
    timer_start(&timer);
    while (!timer_expired(&timer, timeout_ms)) {
        if (!uart_rx_ready()) continue;
        choice = uart_getc_now();
        if (choice == (u8)'1' || choice == (u8)'2') {
            uart_putc((char)choice);
            UART_PUTS_LITERAL("\n");
            return (int)choice;
        }
        /* CR/LF and noise are ignored.  Only an explicit 1 or 2 selects. */
    }
    UART_PUTS_LITERAL("PMOSMENU TIMEOUT\n");
    return 0;
}

static int copy_and_run_recovery(u32 soc_family)
{
    const u8 *source;
    const u8 *end;
    volatile u8 *destination = (volatile u8 *)RECOVERY_LOAD_ADDRESS;
    void (*entry)(void) = (void (*)(void))RECOVERY_ENTRY_ADDRESS;
    u32 size;
    u32 i;

    if (soc_family == RAMLOADER_SOC_LUTON26) {
        source = uart_recovery_luton26_start;
        end = uart_recovery_luton26_end;
    } else if (soc_family == RAMLOADER_SOC_JAGUAR1) {
        source = uart_recovery_jaguar1_start;
        end = uart_recovery_jaguar1_end;
    } else {
        UART_PUTS_LITERAL("PMOSREC UNAVAILABLE UNKNOWN-SOC\n");
        return 0;
    }
    size = (u32)(end - source);
    if (size == 0u || size > CONFIG_UART_RAMLOADER_MAX_SIZE) {
        UART_PUTS_LITERAL("PMOSREC UNAVAILABLE BAD-SIZE\n");
        return 0;
    }
    if (ranges_overlap_physical(RECOVERY_LOAD_ADDRESS, size,
                                CONFIG_UART_STAGE1_ADDRESS,
                                CONFIG_UART_STAGE1_MAX_SIZE)) {
        UART_PUTS_LITERAL("PMOSREC UNAVAILABLE STAGE1-OVERLAP\n");
        return 0;
    }

    UART_PUTS_LITERAL("PMOSREC CHAINLOAD SOC=");
    if (soc_family == RAMLOADER_SOC_LUTON26)
        UART_PUTS_LITERAL("luton26");
    else
        UART_PUTS_LITERAL("jaguar1");
    UART_PUTS_LITERAL(" SIZE="); uart_put_hex32(size);
    UART_PUTS_LITERAL(" ENTRY="); uart_put_hex32(RECOVERY_ENTRY_ADDRESS);
    UART_PUTS_LITERAL("\n");

    for (i = 0u; i < size; i++) destination[i] = source[i];
    cache_prepare_for_execution(RECOVERY_LOAD_ADDRESS, size);
    entry();
    UART_PUTS_LITERAL("PMOSREC RETURNED\n");
    return 0;
}

__attribute__((noreturn)) static void persistent_recovery_menu(u32 soc_family,
                                                                const char *reason)
{
    int choice;
    UART_PUTS_LITERAL("PMOSBOOT FLASH-FAIL ");
    uart_puts(reason);
    UART_PUTS_LITERAL("\n");
    for (;;) {
        choice = uart_menu_selection(CONFIG_UART_MENU_TIMEOUT_MS);
        if (choice == '1') {
            (void)uart_ramloader_run_session(soc_family);
            UART_PUTS_LITERAL("PMOSMENU RAM-LOADER-DONE\n");
        } else if (choice == '2') {
            (void)copy_and_run_recovery(soc_family);
        } else {
            UART_PUTS_LITERAL("PMOSMENU RECOVERY-WAIT\n");
        }
    }
}

UART_STAGE1_INLINE u32 flash_header_crc(const u8 *header, const u8 *payload, u32 size)
{
    static const u8 zero_crc[4] = { 0u, 0u, 0u, 0u };
    u32 crc = 0xffffffffu;
    crc = pmos_crc32_update(crc, header, 16u);
    crc = pmos_crc32_update(crc, zero_crc, 4u);
    crc = pmos_crc32_update(crc, header + 20u, 12u);
    crc = pmos_crc32_update(crc, payload, size);
    return ~crc;
}

__attribute__((noreturn)) static void boot_flash_kernel(u32 loader_base, u32 soc_family)
{
    const u8 *header;
    const u8 *payload;
    u32 magic, load_addr, size, entry_addr, expected_crc, calculated_crc;
    u32 payload_header_addr, i;

    payload_header_addr = (loader_base + CONFIG_LOADER_REGION_SIZE) &
                          ~(CONFIG_LOADER_REGION_SIZE - 1u);
    header = (const u8 *)payload_header_addr;
    magic = get_le32(header + 0u);
    load_addr = get_le32(header + 4u);
    size = get_le32(header + 8u);
    entry_addr = get_le32(header + 12u);
    expected_crc = get_le32(header + 16u);

    UART_PUTS_LITERAL("PMOSBOOT FLASH HEADER=");
    uart_put_hex32(payload_header_addr);
    UART_PUTS_LITERAL("\n");

    if (magic != FLASH_PAYLOAD_MAGIC)
        persistent_recovery_menu(soc_family, "BAD-MAGIC");
    if (size == 0u)
        persistent_recovery_menu(soc_family, "SIZE-ZERO");
    if (size > CONFIG_HARD_PAYLOAD_LIMIT)
        persistent_recovery_menu(soc_family, "SIZE-HARD");
#if defined(CONFIG_SIZE_POLICY_LEGACY_STRICT)
    if (size > CONFIG_LEGACY_PAYLOAD_LIMIT)
        persistent_recovery_menu(soc_family, "SIZE-LEGACY");
#elif defined(CONFIG_SIZE_POLICY_LEGACY_WARN)
    if (size > CONFIG_LEGACY_PAYLOAD_LIMIT)
        UART_PUTS_LITERAL("WARNING: payload exceeds legacy size threshold; continuing within hard slot limit\n");
#endif
    if ((load_addr >> 28) != 8u)
        persistent_recovery_menu(soc_family, "LOAD-ADDRESS");
    if (load_addr + size < load_addr)
        persistent_recovery_menu(soc_family, "RAM-RANGE-OVERFLOW");
    if (ranges_overlap_physical(load_addr, size, CONFIG_UART_STAGE1_ADDRESS,
                                CONFIG_UART_STAGE1_MAX_SIZE))
        persistent_recovery_menu(soc_family, "STAGE1-OVERLAP");
    if (ranges_overlap(load_addr, size, EARLY_STACK_START,
                       EARLY_STACK_END - EARLY_STACK_START))
        persistent_recovery_menu(soc_family, "STACK-OVERLAP");

    payload = header + FLASH_PAYLOAD_HEADER_BYTES;
#if !defined(CONFIG_CRC_POLICY_OFF)
    calculated_crc = flash_header_crc(header, payload, size);
#if defined(CONFIG_CRC_POLICY_STRICT)
    if (calculated_crc != expected_crc)
        persistent_recovery_menu(soc_family, "CRC-MISMATCH");
#elif defined(CONFIG_CRC_POLICY_WARN)
    if (calculated_crc != expected_crc)
        UART_PUTS_LITERAL("WARNING: payload CRC mismatch; continuing because CRC policy is warn\n");
#endif
#else
    (void)expected_crc;
    calculated_crc = 0u;
#endif
    (void)calculated_crc;

    UART_PUTS_LITERAL("PMOSBOOT FLASH-BOOT LOAD=");
    uart_put_hex32(load_addr);
    UART_PUTS_LITERAL(" SIZE=");
    uart_put_hex32(size);
    UART_PUTS_LITERAL(" ENTRY=");
    uart_put_hex32(entry_addr);
    UART_PUTS_LITERAL("\n");

    /* Stage 1 is no longer constrained by the historical 32-byte assembly
     * copy loop.  Copy the exact declared length so legacy no-size images with
     * an unaligned size can still boot safely. */
    for (i = 0u; i < size; i++)
        ((volatile u8 *)load_addr)[i] = payload[i];

    cache_prepare_for_execution(load_addr, size);
    jump_to_kernel(entry_addr);
    persistent_recovery_menu(soc_family, "KERNEL-RETURNED");
}

UART_STAGE1_ENTRY void uart_stage1_entry(u32 soc_family, u32 loader_base)
{
    int choice;

    UART_PUTS_LITERAL("PMOSMENU PROBE SOC=");
    if (soc_family == RAMLOADER_SOC_LUTON26)
        UART_PUTS_LITERAL("luton26");
    else if (soc_family == RAMLOADER_SOC_JAGUAR1)
        UART_PUTS_LITERAL("jaguar1");
    else
        UART_PUTS_LITERAL("unknown");
    UART_PUTS_LITERAL(" PROBE_MS=");
    uart_put_hex32(CONFIG_UART_RAMLOADER_PROBE_TIMEOUT_MS);
    UART_PUTS_LITERAL("\n");

    if (uart_wait_for_interrupt(CONFIG_UART_RAMLOADER_PROBE_TIMEOUT_MS)) {
        choice = uart_menu_selection(CONFIG_UART_MENU_TIMEOUT_MS);
        if (choice == '1') {
            (void)uart_ramloader_run_session(soc_family);
            UART_PUTS_LITERAL("PMOSBOOT UART-DONE\n");
        } else if (choice == '2') {
            (void)copy_and_run_recovery(soc_family);
            persistent_recovery_menu(soc_family, "RECOVERY-RETURNED");
        }
    }
    boot_flash_kernel(loader_base, soc_family);
}
