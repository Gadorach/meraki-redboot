/*
 * Bench UART RAM loader for VCore-III LinuxLoader.
 *
 * Protocol v2 is acknowledged and chunked. Every receive wait is bounded, each
 * chunk has CRC-32, and the complete executable is checked with CRC-32 and
 * SHA-256 before cache maintenance and execution.
 */

#include <postmerkos_uart_crypto.h>

typedef pmos_u8 u8;
typedef pmos_u32 u32;

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

#ifndef CONFIG_HARD_PAYLOAD_LIMIT
#define CONFIG_HARD_PAYLOAD_LIMIT 0x003bffe0u
#endif
#ifndef CONFIG_LEGACY_PAYLOAD_LIMIT
#define CONFIG_LEGACY_PAYLOAD_LIMIT CONFIG_HARD_PAYLOAD_LIMIT
#endif
#ifndef CONFIG_UART_RAMLOADER_STAGE1_MAX_SIZE
#define CONFIG_UART_RAMLOADER_STAGE1_MAX_SIZE 0x00100000u
#endif

#define SPIM_LOADER_MAGIC 0x4d495053u
#define SPIM_HEADER_BYTES 32u
#define EMBEDDED_RECOVERY_LOAD_ADDR 0x86c00000u
#define EMBEDDED_LIVEBOOT_LOAD_ADDR 0x86c00000u
#define STAGE_STACK_RESERVED_END 0x80004000u

struct spim_header {
    u32 magic;
    u32 load_addr;
    u32 size;
    u32 entry_addr;
    u32 expected_crc32;
    u32 reserved0;
    u32 reserved1;
    u32 reserved2;
};

void uart_stage1_jump_kernel(u32 entry_addr) __attribute__((noreturn));

extern const u8 recovery_luton26_blob_start[];
extern const u8 recovery_luton26_blob_end[];
extern const u8 recovery_jaguar1_blob_start[];
extern const u8 recovery_jaguar1_blob_end[];
extern const u8 liveboot_jaguar1_blob_start[];
extern const u8 liveboot_jaguar1_blob_end[];

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

static volatile u32 * const uart = (volatile u32 *)UART_BASE;

static u32 cp0_count(void)
{
    u32 value;
    __asm__ __volatile__("mfc0 %0, $9" : "=r"(value));
    return value;
}

static void timer_start(struct pmos_timer *timer)
{
    timer->last_count = cp0_count();
    timer->remainder_ticks = 0u;
    timer->elapsed_ms = 0u;
}

static int timer_expired(struct pmos_timer *timer, u32 limit_ms)
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

static int uart_rx_ready(void)
{
    return (uart[UART_LSR / 4] & UART_LSR_DR) != 0;
}

/* The timeout covers the entire requested block, not each individual byte. */
static int uart_recv_exact(u8 *destination, u32 length, u32 milliseconds)
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

static void uart_drain_rx(void)
{
    u32 remaining = 8192u;
    while (remaining-- && uart_rx_ready()) (void)uart[UART_RBR / 4];
}

static void uart_putc(char c)
{
    if (c == '\n') uart_putc('\r');
    while ((uart[UART_LSR / 4] & UART_LSR_THRE) == 0) {}
    uart[UART_THR / 4] = (u32)(u8)c;
}

static void uart_puts(const char *text)
{
    while (*text) uart_putc(*text++);
}

static void uart_put_hex_nibble(u32 value)
{
    value &= 0xfu;
    uart_putc((char)(value < 10u ? ('0' + value) : ('a' + value - 10u)));
}

static void uart_put_hex32(u32 value)
{
    int shift;
    for (shift = 28; shift >= 0; shift -= 4) uart_put_hex_nibble(value >> shift);
}

static void uart_put_hex_nibble_upper(u32 value)
{
    value &= 0xfu;
    uart_putc((char)(value < 10u ? ('0' + value) : ('A' + value - 10u)));
}

static void uart_put_hex32_upper(u32 value)
{
    int shift;
    for (shift = 28; shift >= 0; shift -= 4)
        uart_put_hex_nibble_upper(value >> shift);
}

static void uart_put_hex32_prefixed(u32 value)
{
    uart_puts("0x");
    uart_put_hex32_upper(value);
}

static void uart_put_digest(const u8 *digest, u32 length)
{
    u32 i;
    for (i = 0u; i < length; i++) {
        uart_put_hex_nibble_upper(digest[i] >> 4);
        uart_put_hex_nibble_upper(digest[i]);
    }
}

static void report_prefix(const char *domain, const char *status, const char *check)
{
    uart_puts(domain);
    uart_putc(' ');
    uart_puts(status);
    uart_putc('-');
    uart_puts(check);
    uart_puts(": ");
}

static void report_text(const char *domain, const char *status,
                        const char *check, const char *text)
{
    report_prefix(domain, status, check);
    uart_puts(text);
    uart_puts("\n");
}

static void report_value(const char *domain, const char *status,
                         const char *check, const char *label, u32 value)
{
    report_prefix(domain, status, check);
    uart_puts(label);
    uart_puts(": ");
    uart_put_hex32_prefixed(value);
    uart_puts("\n");
}

static void report_pair(const char *domain, const char *status,
                        const char *check, const char *left_label, u32 left,
                        const char *right_label, u32 right)
{
    report_prefix(domain, status, check);
    uart_puts(left_label);
    uart_puts(": ");
    uart_put_hex32_prefixed(left);
    uart_puts(" | ");
    uart_puts(right_label);
    uart_puts(": ");
    uart_put_hex32_prefixed(right);
    uart_puts("\n");
}

static void report_three(const char *domain, const char *status,
                         const char *check, const char *first_label, u32 first,
                         const char *second_label, u32 second,
                         const char *third_label, u32 third)
{
    report_prefix(domain, status, check);
    uart_puts(first_label);
    uart_puts(": ");
    uart_put_hex32_prefixed(first);
    uart_puts(" | ");
    uart_puts(second_label);
    uart_puts(": ");
    uart_put_hex32_prefixed(second);
    uart_puts(" | ");
    uart_puts(third_label);
    uart_puts(": ");
    uart_put_hex32_prefixed(third);
    uart_puts("\n");
}

static void report_ranges(const char *domain, const char *status,
                          const char *check, const char *first_label,
                          u32 first_start, u32 first_end,
                          const char *second_label,
                          u32 second_start, u32 second_end)
{
    report_prefix(domain, status, check);
    uart_puts(first_label);
    uart_puts(": ");
    uart_put_hex32_prefixed(first_start);
    uart_putc('-');
    uart_put_hex32_prefixed(first_end);
    uart_puts(" | ");
    uart_puts(second_label);
    uart_puts(": ");
    uart_put_hex32_prefixed(second_start);
    uart_putc('-');
    uart_put_hex32_prefixed(second_end);
    uart_puts("\n");
}

static void report_digest_pair(const char *domain, const char *status,
                               const char *check, const u8 *expected,
                               const u8 *got, u32 length)
{
    report_prefix(domain, status, check);
    uart_puts("EXPECTED: ");
    uart_put_digest(expected, length);
    uart_puts(" | GOT: ");
    uart_put_digest(got, length);
    uart_puts("\n");
}

static u32 get_le32(const u8 *data)
{
    return (u32)data[0] | ((u32)data[1] << 8) |
           ((u32)data[2] << 16) | ((u32)data[3] << 24);
}

static void parse_header(struct ram_header *header, const u8 *raw)
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

static void parse_frame_header(struct frame_header *frame, const u8 *raw)
{
    frame->magic = get_le32(raw);
    frame->sequence = get_le32(raw + 4);
    frame->length = get_le32(raw + 8);
    frame->crc32 = get_le32(raw + 12);
}

static int validate_header(const struct ram_header *header, const u8 *raw)
{
    u32 end;
    u32 calculated_header_crc;

    if (header->magic0 != RAMLOADER_MAGIC0) {
        report_pair("PMOSRAM", "FAIL", "HEADER-MAGIC0",
                    "EXPECTED", RAMLOADER_MAGIC0, "GOT", header->magic0);
        return 0;
    }
    report_pair("PMOSRAM", "PASS", "HEADER-MAGIC0",
                "EXPECTED", RAMLOADER_MAGIC0, "GOT", header->magic0);

    if (header->magic1 != RAMLOADER_MAGIC1) {
        report_pair("PMOSRAM", "FAIL", "HEADER-MAGIC1",
                    "EXPECTED", RAMLOADER_MAGIC1, "GOT", header->magic1);
        return 0;
    }
    report_pair("PMOSRAM", "PASS", "HEADER-MAGIC1",
                "EXPECTED", RAMLOADER_MAGIC1, "GOT", header->magic1);

    if (header->version != RAMLOADER_PROTOCOL_VERSION) {
        report_pair("PMOSRAM", "FAIL", "HEADER-VERSION",
                    "EXPECTED", RAMLOADER_PROTOCOL_VERSION, "GOT", header->version);
        return 0;
    }
    report_pair("PMOSRAM", "PASS", "HEADER-VERSION",
                "EXPECTED", RAMLOADER_PROTOCOL_VERSION, "GOT", header->version);

    if (header->flags != 0u) {
        report_pair("PMOSRAM", "FAIL", "HEADER-FLAGS",
                    "EXPECTED", 0u, "GOT", header->flags);
        return 0;
    }
    report_pair("PMOSRAM", "PASS", "HEADER-FLAGS",
                "EXPECTED", 0u, "GOT", header->flags);

    calculated_header_crc = pmos_crc32(raw, RAMLOADER_HEADER_BYTES - 4u);
    if (header->header_crc32 != calculated_header_crc) {
        report_pair("PMOSRAM", "FAIL", "HEADER-CRC",
                    "EXPECTED", header->header_crc32, "GOT", calculated_header_crc);
        return 0;
    }
    report_pair("PMOSRAM", "PASS", "HEADER-CRC",
                "EXPECTED", header->header_crc32, "GOT", calculated_header_crc);

    if (header->load_addr < CONFIG_UART_RAMLOADER_RAM_START) {
        report_pair("PMOSRAM", "FAIL", "LOAD-MIN",
                    "MIN", CONFIG_UART_RAMLOADER_RAM_START, "GOT", header->load_addr);
        return 0;
    }
    report_pair("PMOSRAM", "PASS", "LOAD-MIN",
                "MIN", CONFIG_UART_RAMLOADER_RAM_START, "GOT", header->load_addr);

    if (header->total_size == 0u) {
        report_pair("PMOSRAM", "FAIL", "SIZE-NONZERO",
                    "MIN", 1u, "GOT", header->total_size);
        return 0;
    }
    report_pair("PMOSRAM", "PASS", "SIZE-NONZERO",
                "MIN", 1u, "GOT", header->total_size);

    if (header->total_size > CONFIG_UART_RAMLOADER_MAX_SIZE) {
        report_pair("PMOSRAM", "FAIL", "SIZE-MAX",
                    "MAX", CONFIG_UART_RAMLOADER_MAX_SIZE, "GOT", header->total_size);
        return 0;
    }
    report_pair("PMOSRAM", "PASS", "SIZE-MAX",
                "MAX", CONFIG_UART_RAMLOADER_MAX_SIZE, "GOT", header->total_size);

    end = header->load_addr + header->total_size;
    if (end < header->load_addr) {
        report_pair("PMOSRAM", "FAIL", "LOAD-OVERFLOW",
                    "START", header->load_addr, "END", end);
        return 0;
    }
    report_pair("PMOSRAM", "PASS", "LOAD-OVERFLOW",
                "START", header->load_addr, "END", end);

    if (end > CONFIG_UART_RAMLOADER_RAM_END) {
        report_pair("PMOSRAM", "FAIL", "LOAD-END",
                    "MAX", CONFIG_UART_RAMLOADER_RAM_END, "GOT", end);
        return 0;
    }
    report_pair("PMOSRAM", "PASS", "LOAD-END",
                "MAX", CONFIG_UART_RAMLOADER_RAM_END, "GOT", end);

    if ((header->load_addr & 3u) != 0u) {
        report_pair("PMOSRAM", "FAIL", "LOAD-ALIGN",
                    "EXPECTED-MASK", 0u, "GOT", header->load_addr & 3u);
        return 0;
    }
    report_pair("PMOSRAM", "PASS", "LOAD-ALIGN",
                "EXPECTED-MASK", 0u, "GOT", header->load_addr & 3u);

    if ((header->entry_addr & 3u) != 0u) {
        report_pair("PMOSRAM", "FAIL", "ENTRY-ALIGN",
                    "EXPECTED-MASK", 0u, "GOT", header->entry_addr & 3u);
        return 0;
    }
    report_pair("PMOSRAM", "PASS", "ENTRY-ALIGN",
                "EXPECTED-MASK", 0u, "GOT", header->entry_addr & 3u);

    if (header->entry_addr < header->load_addr || header->entry_addr >= end) {
        report_three("PMOSRAM", "FAIL", "ENTRY-RANGE",
                     "START", header->load_addr, "END", end,
                     "ENTRY", header->entry_addr);
        return 0;
    }
    report_three("PMOSRAM", "PASS", "ENTRY-RANGE",
                 "START", header->load_addr, "END", end,
                 "ENTRY", header->entry_addr);

    if (header->chunk_size < 64u || header->chunk_size > RAMLOADER_MAX_CHUNK) {
        report_three("PMOSRAM", "FAIL", "CHUNK-SIZE",
                     "MIN", 64u, "MAX", RAMLOADER_MAX_CHUNK,
                     "GOT", header->chunk_size);
        return 0;
    }
    report_three("PMOSRAM", "PASS", "CHUNK-SIZE",
                 "MIN", 64u, "MAX", RAMLOADER_MAX_CHUNK,
                 "GOT", header->chunk_size);
    return 1;
}

static void send_sequence_status(const char *prefix, u32 sequence)
{
    uart_puts(prefix);
    uart_put_hex32(sequence);
    uart_puts("\n");
}

static void cache_prepare_for_execution(u32 start, u32 size)
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

static void uart_put_label_hex(const char *label, u32 value)
{
    uart_puts(label);
    uart_put_hex32(value);
}

static int ranges_overlap(u32 start_a, u32 end_a, u32 start_b, u32 end_b)
{
    return start_a < end_b && start_b < end_a;
}

static int uart_ramloader_wait_and_run(u32 soc_family, u32 probe_timeout_ms);

static void persistent_uart_ramloader(u32 soc_family, const char *source)
    __attribute__((noreturn));
static void persistent_uart_ramloader(u32 soc_family, const char *source)
{
    report_prefix("PMOSBOOT", "INFO", "RAMLOADER");
    uart_puts("SOURCE: ");
    uart_puts(source);
    uart_puts(" | MODE: PERSISTENT | EXIT: POWER-CYCLE\n");
    for (;;) {
        uart_drain_rx();
        (void)uart_ramloader_wait_and_run(soc_family, 0u);
        report_text("PMOSRAM", "INFO", "RETRY",
                    "TRANSFER ABORTED OR PAYLOAD RETURNED");
    }
}

static void launch_embedded_recovery(u32 soc_family, const char *source_check)
    __attribute__((noreturn));
static void launch_embedded_recovery(u32 soc_family, const char *source_check)
{
    const u8 *source;
    const u8 *end;
    volatile u8 *destination = (volatile u8 *)EMBEDDED_RECOVERY_LOAD_ADDR;
    u32 size;
    u32 i;

    if (soc_family == RAMLOADER_SOC_LUTON26) {
        source = recovery_luton26_blob_start;
        end = recovery_luton26_blob_end;
    } else if (soc_family == RAMLOADER_SOC_JAGUAR1) {
        source = recovery_jaguar1_blob_start;
        end = recovery_jaguar1_blob_end;
    } else {
        report_pair("PMOSBOOT", "FAIL", "RECOVERY-SOC",
                    "EXPECTED", RAMLOADER_SOC_JAGUAR1, "GOT", soc_family);
        persistent_uart_ramloader(soc_family, "UNKNOWN-SOC");
    }

    size = (u32)end - (u32)source;
    report_prefix("PMOSBOOT", "INFO", "RECOVERY");
    uart_puts("SOURCE: ");
    uart_puts(source_check);
    uart_puts(" | SOC: ");
    uart_puts(soc_family == RAMLOADER_SOC_JAGUAR1 ? "jaguar1" : "luton26");
    uart_puts("\n");

    if (size == 0u || size > CONFIG_UART_RAMLOADER_MAX_SIZE) {
        report_pair("PMOSBOOT", "FAIL", "RECOVERY-SIZE",
                    "MAX", CONFIG_UART_RAMLOADER_MAX_SIZE, "GOT", size);
        persistent_uart_ramloader(soc_family, "EMBEDDED-RECOVERY-SIZE");
    }
    report_pair("PMOSBOOT", "PASS", "RECOVERY-SIZE",
                "MAX", CONFIG_UART_RAMLOADER_MAX_SIZE, "GOT", size);

    uart_drain_rx();
    for (i = 0u; i < size; i++) destination[i] = source[i];
    report_pair("PMOSBOOT", "PASS", "RECOVERY-COPY",
                "LOAD", EMBEDDED_RECOVERY_LOAD_ADDR, "SIZE", size);
    cache_prepare_for_execution(EMBEDDED_RECOVERY_LOAD_ADDR, size);
    report_value("PMOSBOOT", "PASS", "RECOVERY-EXEC",
                 "ENTRY", EMBEDDED_RECOVERY_LOAD_ADDR);
    uart_stage1_jump_kernel(EMBEDDED_RECOVERY_LOAD_ADDR);
}

static void launch_embedded_liveboot(u32 soc_family, const char *source_check)
    __attribute__((noreturn));
static void launch_embedded_liveboot(u32 soc_family, const char *source_check)
{
    const u8 *source = liveboot_jaguar1_blob_start;
    const u8 *end = liveboot_jaguar1_blob_end;
    volatile u8 *destination = (volatile u8 *)EMBEDDED_LIVEBOOT_LOAD_ADDR;
    u32 size;
    u32 i;

    if (soc_family != RAMLOADER_SOC_JAGUAR1) {
        report_pair("PMOSBOOT", "FAIL", "LIVEBOOT-SOC",
                    "EXPECTED", RAMLOADER_SOC_JAGUAR1, "GOT", soc_family);
        persistent_uart_ramloader(soc_family, "LIVEBOOT-UNSUPPORTED-SOC");
    }
    size = (u32)end - (u32)source;
    report_prefix("PMOSBOOT", "INFO", "LIVEBOOT");
    uart_puts("SOURCE: "); uart_puts(source_check);
    uart_puts(" | SOC: jaguar1 | FLASH: DISABLED\n");
    if (size == 0u || size > CONFIG_UART_RAMLOADER_MAX_SIZE) {
        report_pair("PMOSBOOT", "FAIL", "LIVEBOOT-SIZE",
                    "MAX", CONFIG_UART_RAMLOADER_MAX_SIZE, "GOT", size);
        persistent_uart_ramloader(soc_family, "EMBEDDED-LIVEBOOT-SIZE");
    }
    uart_drain_rx();
    for (i = 0u; i < size; i++) destination[i] = source[i];
    report_pair("PMOSBOOT", "PASS", "LIVEBOOT-COPY",
                "LOAD", EMBEDDED_LIVEBOOT_LOAD_ADDR, "SIZE", size);
    cache_prepare_for_execution(EMBEDDED_LIVEBOOT_LOAD_ADDR, size);
    report_value("PMOSBOOT", "PASS", "LIVEBOOT-EXEC",
                 "ENTRY", EMBEDDED_LIVEBOOT_LOAD_ADDR);
    uart_stage1_jump_kernel(EMBEDDED_LIVEBOOT_LOAD_ADDR);
}

static void read_spim_header(struct spim_header *header, u8 raw[SPIM_HEADER_BYTES],
                             u32 payload_header_addr)
{
    const volatile u8 *source = (const volatile u8 *)payload_header_addr;
    u32 i;
    for (i = 0u; i < SPIM_HEADER_BYTES; i++) raw[i] = source[i];
    header->magic = get_le32(raw + 0u);
    header->load_addr = get_le32(raw + 4u);
    header->size = get_le32(raw + 8u);
    header->entry_addr = get_le32(raw + 12u);
    header->expected_crc32 = get_le32(raw + 16u);
    header->reserved0 = get_le32(raw + 20u);
    header->reserved1 = get_le32(raw + 24u);
    header->reserved2 = get_le32(raw + 28u);
}

static void boot_flash_kernel(u32 soc_family, u32 payload_header_addr)
    __attribute__((noreturn));
static void boot_flash_kernel(u32 soc_family, u32 payload_header_addr)
{
    struct spim_header header;
    u8 raw[SPIM_HEADER_BYTES];
    volatile const u32 *source_words;
    volatile u32 *destination_words;
    const u8 *payload_bytes;
    u32 load_end;
    u32 copy_size;
    u32 overlap;
    u32 stage_cached_start = CONFIG_UART_RAMLOADER_RAM_END;
    u32 stage_cached_end = stage_cached_start + CONFIG_UART_RAMLOADER_STAGE1_MAX_SIZE;
    u32 offset;
#if !defined(CONFIG_CRC_POLICY_OFF)
    u32 crc;
#endif

    report_value("PMOSBOOT", "PASS", "HEADER-ADDRESS",
                 "ADDRESS", payload_header_addr);
    read_spim_header(&header, raw, payload_header_addr);

    if (header.magic != SPIM_LOADER_MAGIC) {
        report_pair("PMOSBOOT", "FAIL", "MAGIC",
                    "EXPECTED", SPIM_LOADER_MAGIC, "GOT", header.magic);
        launch_embedded_recovery(soc_family, "MAGIC");
    }
    report_pair("PMOSBOOT", "PASS", "MAGIC",
                "EXPECTED", SPIM_LOADER_MAGIC, "GOT", header.magic);

    if (header.size == 0u) {
        report_pair("PMOSBOOT", "FAIL", "SIZE-NONZERO",
                    "MIN", 1u, "GOT", header.size);
        launch_embedded_recovery(soc_family, "SIZE-NONZERO");
    }
    report_pair("PMOSBOOT", "PASS", "SIZE-NONZERO",
                "MIN", 1u, "GOT", header.size);

    if (header.size > 0xffffffe0u) {
        report_pair("PMOSBOOT", "FAIL", "SIZE-ROUND",
                    "MAX", 0xffffffe0u, "GOT", header.size);
        launch_embedded_recovery(soc_family, "SIZE-ROUND");
    }
    copy_size = (header.size + 31u) & ~31u;

    if ((header.size & 31u) != 0u) {
#if defined(CONFIG_SIZE_POLICY_LEGACY_STRICT)
        report_pair("PMOSBOOT", "FAIL", "SIZE-ALIGN",
                    "DECLARED", header.size, "EFFECTIVE", copy_size);
        launch_embedded_recovery(soc_family, "SIZE-ALIGN");
#else
        report_pair("PMOSBOOT", "WARN", "SIZE-ALIGN",
                    "DECLARED", header.size, "EFFECTIVE", copy_size);
#endif
    } else {
        report_pair("PMOSBOOT", "PASS", "SIZE-ALIGN",
                    "DECLARED", header.size, "EFFECTIVE", copy_size);
    }

    if (copy_size > CONFIG_HARD_PAYLOAD_LIMIT) {
        report_pair("PMOSBOOT", "FAIL", "SIZE-HARD",
                    "LIMIT", CONFIG_HARD_PAYLOAD_LIMIT, "EFFECTIVE", copy_size);
        launch_embedded_recovery(soc_family, "SIZE-HARD");
    }
    report_pair("PMOSBOOT", "PASS", "SIZE-HARD",
                "LIMIT", CONFIG_HARD_PAYLOAD_LIMIT, "EFFECTIVE", copy_size);

#if defined(CONFIG_SIZE_POLICY_LEGACY_STRICT)
    if (copy_size > CONFIG_LEGACY_PAYLOAD_LIMIT) {
        report_pair("PMOSBOOT", "FAIL", "SIZE-LEGACY",
                    "LIMIT", CONFIG_LEGACY_PAYLOAD_LIMIT, "EFFECTIVE", copy_size);
        launch_embedded_recovery(soc_family, "SIZE-LEGACY");
    }
    report_pair("PMOSBOOT", "PASS", "SIZE-LEGACY",
                "LIMIT", CONFIG_LEGACY_PAYLOAD_LIMIT, "EFFECTIVE", copy_size);
#elif defined(CONFIG_SIZE_POLICY_LEGACY_WARN)
    if (copy_size > CONFIG_LEGACY_PAYLOAD_LIMIT)
        report_pair("PMOSBOOT", "WARN", "SIZE-LEGACY",
                    "LIMIT", CONFIG_LEGACY_PAYLOAD_LIMIT, "EFFECTIVE", copy_size);
    else
        report_pair("PMOSBOOT", "PASS", "SIZE-LEGACY",
                    "LIMIT", CONFIG_LEGACY_PAYLOAD_LIMIT, "EFFECTIVE", copy_size);
#else
    report_pair("PMOSBOOT", "SKIP", "SIZE-LEGACY",
                "LIMIT", CONFIG_LEGACY_PAYLOAD_LIMIT, "EFFECTIVE", copy_size);
#endif

    if ((header.load_addr >> 28) != 8u) {
        report_pair("PMOSBOOT", "FAIL", "LOAD-SEGMENT",
                    "EXPECTED", 0x80000000u,
                    "GOT", header.load_addr & 0xf0000000u);
        launch_embedded_recovery(soc_family, "LOAD-SEGMENT");
    }
    report_pair("PMOSBOOT", "PASS", "LOAD-SEGMENT",
                "EXPECTED", 0x80000000u,
                "GOT", header.load_addr & 0xf0000000u);

    if ((header.load_addr & 3u) != 0u) {
        report_pair("PMOSBOOT", "FAIL", "LOAD-ALIGN",
                    "EXPECTED-MASK", 0u, "GOT", header.load_addr & 3u);
        launch_embedded_recovery(soc_family, "LOAD-ALIGN");
    }
    report_pair("PMOSBOOT", "PASS", "LOAD-ALIGN",
                "EXPECTED-MASK", 0u, "GOT", header.load_addr & 3u);

    load_end = header.load_addr + copy_size;
    if (load_end < header.load_addr) {
        report_pair("PMOSBOOT", "FAIL", "LOAD-OVERFLOW",
                    "START", header.load_addr, "END", load_end);
        launch_embedded_recovery(soc_family, "LOAD-OVERFLOW");
    }
    report_pair("PMOSBOOT", "PASS", "LOAD-OVERFLOW",
                "START", header.load_addr, "END", load_end);

    if ((header.entry_addr & 3u) != 0u) {
        report_pair("PMOSBOOT", "FAIL", "ENTRY-ALIGN",
                    "EXPECTED-MASK", 0u, "GOT", header.entry_addr & 3u);
        launch_embedded_recovery(soc_family, "ENTRY-ALIGN");
    }
    report_pair("PMOSBOOT", "PASS", "ENTRY-ALIGN",
                "EXPECTED-MASK", 0u, "GOT", header.entry_addr & 3u);

    if (header.entry_addr < header.load_addr || header.entry_addr >= load_end) {
        report_three("PMOSBOOT", "FAIL", "ENTRY-RANGE",
                     "START", header.load_addr, "END", load_end,
                     "ENTRY", header.entry_addr);
        launch_embedded_recovery(soc_family, "ENTRY-RANGE");
    }
    report_three("PMOSBOOT", "PASS", "ENTRY-RANGE",
                 "START", header.load_addr, "END", load_end,
                 "ENTRY", header.entry_addr);

    overlap = (u32)ranges_overlap(header.load_addr, load_end,
                                  0x80000000u, STAGE_STACK_RESERVED_END);
    report_ranges("PMOSBOOT", overlap ? "FAIL" : "PASS", "STACK-OVERLAP",
                  "IMAGE", header.load_addr, load_end,
                  "RESERVED", 0x80000000u, STAGE_STACK_RESERVED_END);
    if (overlap)
        launch_embedded_recovery(soc_family, "STACK-OVERLAP");

    overlap = (u32)ranges_overlap(header.load_addr, load_end,
                                  stage_cached_start, stage_cached_end);
    report_ranges("PMOSBOOT", overlap ? "FAIL" : "PASS", "STAGE1-OVERLAP",
                  "IMAGE", header.load_addr, load_end,
                  "RESERVED", stage_cached_start, stage_cached_end);
    if (overlap)
        launch_embedded_recovery(soc_family, "STAGE1-OVERLAP");

    report_three("PMOSBOOT", "PASS", "KERNEL-PLAN",
                 "LOAD", header.load_addr, "DECLARED", header.size,
                 "EFFECTIVE", copy_size);
    report_value("PMOSBOOT", "PASS", "KERNEL-ENTRY",
                 "ENTRY", header.entry_addr);

    payload_bytes = (const u8 *)(payload_header_addr + SPIM_HEADER_BYTES);
    source_words = (const volatile u32 *)payload_bytes;
    destination_words = (volatile u32 *)header.load_addr;

#if !defined(CONFIG_CRC_POLICY_OFF)
    raw[16] = 0u; raw[17] = 0u; raw[18] = 0u; raw[19] = 0u;
    crc = pmos_crc32_update(0xffffffffu, raw, SPIM_HEADER_BYTES);
#endif
    for (offset = 0u; offset < copy_size; offset += 32u) {
#if !defined(CONFIG_CRC_POLICY_OFF)
        crc = pmos_crc32_update(crc, payload_bytes + offset, 32u);
#endif
        destination_words[0] = source_words[0];
        destination_words[1] = source_words[1];
        destination_words[2] = source_words[2];
        destination_words[3] = source_words[3];
        destination_words[4] = source_words[4];
        destination_words[5] = source_words[5];
        destination_words[6] = source_words[6];
        destination_words[7] = source_words[7];
        source_words += 8;
        destination_words += 8;
    }
    report_pair("PMOSBOOT", "PASS", "COPY",
                "DECLARED", header.size, "COPIED", copy_size);

#if defined(CONFIG_CRC_POLICY_STRICT)
    crc = ~crc;
    if (crc != header.expected_crc32) {
        report_pair("PMOSBOOT", "FAIL", "CRC",
                    "EXPECTED", header.expected_crc32, "GOT", crc);
        launch_embedded_recovery(soc_family, "CRC");
    }
    report_pair("PMOSBOOT", "PASS", "CRC",
                "EXPECTED", header.expected_crc32, "GOT", crc);
#elif defined(CONFIG_CRC_POLICY_WARN)
    crc = ~crc;
    if (crc != header.expected_crc32)
        report_pair("PMOSBOOT", "WARN", "CRC",
                    "EXPECTED", header.expected_crc32, "GOT", crc);
    else
        report_pair("PMOSBOOT", "PASS", "CRC",
                    "EXPECTED", header.expected_crc32, "GOT", crc);
#else
    report_value("PMOSBOOT", "SKIP", "CRC",
                 "EXPECTED", header.expected_crc32);
#endif

    cache_prepare_for_execution(header.load_addr, copy_size);
    report_pair("PMOSBOOT", "PASS", "CACHE",
                "START", header.load_addr, "SIZE", copy_size);
    report_value("PMOSBOOT", "PASS", "EXEC",
                 "ENTRY", header.entry_addr);
    uart_stage1_jump_kernel(header.entry_addr);
}

static int abort_to_boot(const char *reason)
{
    uart_puts("PMOSRAM ABORT ");
    uart_puts(reason);
    uart_puts("\n");
    uart_drain_rx();
    return 0;
}

static int uart_ramloader_wait_and_run(u32 soc_family, u32 probe_timeout_ms)
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
    struct pmos_timer probe_timer, transfer_timer;
    u8 *destination;
    void (*entry)(void);

    uart_puts("PMOSRAM READY 2 SOC=");
    uart_puts(soc_family == RAMLOADER_SOC_LUTON26 ? "luton26" :
              (soc_family == RAMLOADER_SOC_JAGUAR1 ? "jaguar1" : "unknown"));
    uart_puts(" MAX="); uart_put_hex32(CONFIG_UART_RAMLOADER_MAX_SIZE);
    uart_puts(" RAM="); uart_put_hex32(CONFIG_UART_RAMLOADER_RAM_START);
    uart_putc('-'); uart_put_hex32(CONFIG_UART_RAMLOADER_RAM_END);
    uart_puts(" CHUNK="); uart_put_hex32(RAMLOADER_MAX_CHUNK);
    uart_puts(" PROBE_MS=");
    if (probe_timeout_ms == 0u)
        uart_puts("FOREVER");
    else
        uart_put_hex32(probe_timeout_ms);
    uart_puts("\n");

    if (probe_timeout_ms != 0u) timer_start(&probe_timer);
    while (!uart_rx_ready()) {
        if (probe_timeout_ms != 0u && timer_expired(&probe_timer, probe_timeout_ms))
            return 0;
    }

    if (!uart_recv_exact(raw, RAMLOADER_HEADER_BYTES, CONFIG_UART_RAMLOADER_INTERBYTE_TIMEOUT_MS))
        return abort_to_boot("HEADER-TIMEOUT");
    parse_header(&header, raw);
    if (!validate_header(&header, raw)) return abort_to_boot("INVALID-HEADER");

    uart_puts("PMOSRAM HEADER-ACK SIZE="); uart_put_hex32(header.total_size);
    uart_puts(" LOAD="); uart_put_hex32(header.load_addr);
    uart_puts(" ENTRY="); uart_put_hex32(header.entry_addr); uart_puts("\n");

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
            return abort_to_boot("TRANSFER-TIMEOUT");
        if (!uart_recv_exact(frame_raw, RAMLOADER_FRAME_HEADER_BYTES,
                             CONFIG_UART_RAMLOADER_INTERBYTE_TIMEOUT_MS))
            return abort_to_boot("FRAME-HEADER-TIMEOUT");
        parse_frame_header(&frame, frame_raw);
        if (frame.magic != RAMLOADER_FRAME_MAGIC) {
            report_pair("PMOSRAM", "FAIL", "FRAME-MAGIC",
                        "EXPECTED", RAMLOADER_FRAME_MAGIC, "GOT", frame.magic);
            return abort_to_boot("INVALID-FRAME-MAGIC");
        }
        if (frame.length == 0u || frame.length > header.chunk_size ||
            frame.length > RAMLOADER_MAX_CHUNK) {
            report_three("PMOSRAM", "FAIL", "FRAME-LENGTH",
                         "CHUNK-MAX", header.chunk_size,
                         "PROTOCOL-MAX", RAMLOADER_MAX_CHUNK,
                         "GOT", frame.length);
            return abort_to_boot("INVALID-FRAME-LENGTH");
        }
        if (!uart_recv_exact(chunk, frame.length, CONFIG_UART_RAMLOADER_INTERBYTE_TIMEOUT_MS))
            return abort_to_boot("FRAME-DATA-TIMEOUT");
        frame_crc = pmos_crc32(chunk, frame.length);
        if (expected_sequence != 0u && frame.sequence == expected_sequence - 1u) {
            if (frame.length == previous_length && frame.crc32 == previous_crc &&
                frame_crc == frame.crc32)
                send_sequence_status("PMOSRAM ACK ", frame.sequence);
            else
                send_sequence_status("PMOSRAM NACK ", expected_sequence);
            continue;
        }
        if (frame.sequence != expected_sequence) {
            report_pair("PMOSRAM", "WARN", "FRAME-SEQUENCE",
                        "EXPECTED", expected_sequence, "GOT", frame.sequence);
            send_sequence_status("PMOSRAM NACK ", expected_sequence);
            continue;
        }
        if (frame_crc != frame.crc32) {
            report_pair("PMOSRAM", "WARN", "FRAME-CRC",
                        "EXPECTED", frame.crc32, "GOT", frame_crc);
            send_sequence_status("PMOSRAM NACK ", expected_sequence);
            continue;
        }
        if (frame.length > header.total_size - received) {
            report_pair("PMOSRAM", "WARN", "FRAME-REMAINING",
                        "REMAINING", header.total_size - received,
                        "GOT", frame.length);
            send_sequence_status("PMOSRAM NACK ", expected_sequence);
            continue;
        }
        for (i = 0; i < frame.length; i++) destination[received + i] = chunk[i];
        image_crc = pmos_crc32_update(image_crc, chunk, frame.length);
        pmos_sha256_update(&sha, chunk, frame.length);
        received += frame.length;
        previous_length = frame.length;
        previous_crc = frame.crc32;
        send_sequence_status("PMOSRAM ACK ", expected_sequence);
        expected_sequence++;
    }

    image_crc = ~image_crc;
    pmos_sha256_final(&sha, digest);
    if (image_crc != header.image_crc32) {
        report_pair("PMOSRAM", "FAIL", "IMAGE-CRC",
                    "EXPECTED", header.image_crc32, "GOT", image_crc);
        return abort_to_boot("IMAGE-CRC-MISMATCH");
    }
    report_pair("PMOSRAM", "PASS", "IMAGE-CRC",
                "EXPECTED", header.image_crc32, "GOT", image_crc);

    if (!pmos_digest_equal(digest, header.image_sha256, 32u)) {
        report_digest_pair("PMOSRAM", "FAIL", "IMAGE-SHA256",
                           header.image_sha256, digest, 32u);
        return abort_to_boot("IMAGE-SHA256-MISMATCH");
    }
    report_digest_pair("PMOSRAM", "PASS", "IMAGE-SHA256",
                       header.image_sha256, digest, 32u);
    /* Preserve protocol-v2 completion markers used by existing host senders. */
    uart_puts("PMOSRAM VERIFIED SHA256=");
    uart_put_digest(digest, 32u);
    uart_puts("\n");

    report_value("PMOSRAM", "PASS", "EXEC", "ENTRY", header.entry_addr);
    uart_puts("PMOSRAM EXEC ");
    uart_put_hex32(header.entry_addr);
    uart_puts("\n");
    cache_prepare_for_execution(header.load_addr, header.total_size);
    entry = (void (*)(void))header.entry_addr;
    entry();
    uart_puts("PMOSRAM RETURNED\n");
    return 0;
}

void uart_stage1_main(u32 soc_family, u32 payload_header_addr,
                      u32 fallback_entry, u32 loader_runtime_base)
    __attribute__((noreturn));
void uart_stage1_main(u32 soc_family, u32 payload_header_addr,
                      u32 fallback_entry, u32 loader_runtime_base)
{
    uart_puts("PMOSBOOT CONTEXT LOADER=");
    uart_put_hex32(loader_runtime_base);
    uart_puts(" PAYLOAD=");
    uart_put_hex32(payload_header_addr);
    uart_puts(" FALLBACK=");
    uart_put_hex32(fallback_entry);
    uart_puts("\n");
    {
        u8 trigger;
        u8 choice;
        struct pmos_timer menu_timer;

        uart_puts("PMOSBOOT MENU-PROBE TIMEOUT_MS=");
        uart_put_hex32(CONFIG_UART_RAMLOADER_PROBE_TIMEOUT_MS);
        uart_puts("\n");
        if (!uart_recv_exact(&trigger, 1u, CONFIG_UART_RAMLOADER_PROBE_TIMEOUT_MS)) {
            report_text("PMOSBOOT", "PASS", "MENU-PROBE",
                        "NO INPUT; CONTINUING NORMAL BOOT");
            boot_flash_kernel(soc_family, payload_header_addr);
        }

        report_value("PMOSBOOT", "PASS", "MENU-TRIGGER", "BYTE", trigger);
        uart_drain_rx();
        uart_puts("PMOSBOOT MENU 1=UART-RAMLOADER 2=FW-RECOVERY 3=LIVEBOOT\n");
        uart_puts("PMOSBOOT MENU-READY TIMEOUT_MS=");
        uart_put_hex32(CONFIG_UART_MENU_TIMEOUT_MS);
        uart_puts("\n");
        timer_start(&menu_timer);
        for (;;) {
            if (uart_rx_ready()) {
                choice = (u8)(uart[UART_RBR / 4] & 0xffu);
                if (choice == (u8)'1') {
                    report_value("PMOSBOOT", "PASS", "MENU-CHOICE",
                                 "SELECTED", 1u);
                    persistent_uart_ramloader(soc_family, "MENU-OPTION-1");
                }
                if (choice == (u8)'2') {
                    report_value("PMOSBOOT", "PASS", "MENU-CHOICE",
                                 "SELECTED", 2u);
                    launch_embedded_recovery(soc_family, "MENU-OPTION-2");
                }
                if (choice == (u8)'3') {
                    report_value("PMOSBOOT", "PASS", "MENU-CHOICE",
                                 "SELECTED", 3u);
                    launch_embedded_liveboot(soc_family, "MENU-OPTION-3");
                }
                if (choice != (u8)'\r' && choice != (u8)'\n')
                    report_value("PMOSBOOT", "WARN", "MENU-INPUT",
                                 "IGNORED", (u32)choice);
            }
            if (timer_expired(&menu_timer, CONFIG_UART_MENU_TIMEOUT_MS)) {
                report_text("PMOSBOOT", "WARN", "MENU-TIMEOUT",
                            "NO EXPLICIT 1/2/3 SELECTION; CONTINUING NORMAL BOOT");
                boot_flash_kernel(soc_family, payload_header_addr);
            }
        }
    }
}
