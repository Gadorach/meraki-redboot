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
#define STAGE_REASON_PAYLOAD 6u
#define STAGE_REASON_PAYLOAD_RETURNED 7u
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
void uart_stage1_jump_fallback(u32 fallback_entry, u32 reason) __attribute__((noreturn));

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

static void fallback_to_next_region(u32 fallback_entry, u32 reason, const char *message)
    __attribute__((noreturn));
static void fallback_to_next_region(u32 fallback_entry, u32 reason, const char *message)
{
    uart_puts("PMOSBOOT FAIL ");
    uart_puts(message);
    uart_puts(" FALLBACK=");
    uart_put_hex32(fallback_entry);
    uart_puts("\n");
    uart_stage1_jump_fallback(fallback_entry, reason);
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

static void boot_flash_kernel(u32 payload_header_addr, u32 fallback_entry)
    __attribute__((noreturn));
static void boot_flash_kernel(u32 payload_header_addr, u32 fallback_entry)
{
    struct spim_header header;
    u8 raw[SPIM_HEADER_BYTES];
    volatile const u32 *source_words;
    volatile u32 *destination_words;
    const u8 *payload_bytes;
    u32 load_end;
    u32 stage_cached_start = CONFIG_UART_RAMLOADER_RAM_END;
    u32 stage_cached_end = stage_cached_start + CONFIG_UART_RAMLOADER_STAGE1_MAX_SIZE;
    u32 offset;
#if !defined(CONFIG_CRC_POLICY_OFF)
    u32 crc;
#endif

    uart_puts("PMOSBOOT FLASH HEADER=");
    uart_put_hex32(payload_header_addr);
    uart_puts("\n");
    read_spim_header(&header, raw, payload_header_addr);

    if (header.magic != SPIM_LOADER_MAGIC)
        fallback_to_next_region(fallback_entry, STAGE_REASON_PAYLOAD, "MAGIC");
    if (header.size == 0u || (header.size & 31u) != 0u)
        fallback_to_next_region(fallback_entry, STAGE_REASON_PAYLOAD, "SIZE-ALIGN");
    if (header.size > CONFIG_HARD_PAYLOAD_LIMIT)
        fallback_to_next_region(fallback_entry, STAGE_REASON_PAYLOAD, "SIZE-HARD");
#if defined(CONFIG_SIZE_POLICY_LEGACY_STRICT)
    if (header.size > CONFIG_LEGACY_PAYLOAD_LIMIT)
        fallback_to_next_region(fallback_entry, STAGE_REASON_PAYLOAD, "SIZE-LEGACY");
#elif defined(CONFIG_SIZE_POLICY_LEGACY_WARN)
    if (header.size > CONFIG_LEGACY_PAYLOAD_LIMIT)
        uart_puts("WARNING: payload exceeds legacy size threshold; continuing within hard slot limit\n");
#endif
    if ((header.load_addr >> 28) != 8u)
        fallback_to_next_region(fallback_entry, STAGE_REASON_PAYLOAD, "LOAD-ADDRESS");
    load_end = header.load_addr + header.size;
    if (load_end < header.load_addr)
        fallback_to_next_region(fallback_entry, STAGE_REASON_PAYLOAD, "LOAD-OVERFLOW");
    if (ranges_overlap(header.load_addr, load_end, 0x80000000u, STAGE_STACK_RESERVED_END))
        fallback_to_next_region(fallback_entry, STAGE_REASON_PAYLOAD, "STACK-OVERLAP");
    if (ranges_overlap(header.load_addr, load_end, stage_cached_start, stage_cached_end))
        fallback_to_next_region(fallback_entry, STAGE_REASON_PAYLOAD, "STAGE1-OVERLAP");

    uart_puts("PMOSBOOT KERNEL");
    uart_put_label_hex(" LOAD=", header.load_addr);
    uart_put_label_hex(" SIZE=", header.size);
    uart_put_label_hex(" ENTRY=", header.entry_addr);
    uart_puts("\n");

    payload_bytes = (const u8 *)(payload_header_addr + SPIM_HEADER_BYTES);
    source_words = (const volatile u32 *)payload_bytes;
    destination_words = (volatile u32 *)header.load_addr;

#if !defined(CONFIG_CRC_POLICY_OFF)
    raw[16] = 0u; raw[17] = 0u; raw[18] = 0u; raw[19] = 0u;
    crc = pmos_crc32_update(0xffffffffu, raw, SPIM_HEADER_BYTES);
#endif
    for (offset = 0u; offset < header.size; offset += 32u) {
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

#if defined(CONFIG_CRC_POLICY_STRICT)
    crc = ~crc;
    if (crc != header.expected_crc32)
        fallback_to_next_region(fallback_entry, STAGE_REASON_PAYLOAD, "CRC32");
#elif defined(CONFIG_CRC_POLICY_WARN)
    crc = ~crc;
    if (crc != header.expected_crc32)
        uart_puts("WARNING: payload CRC mismatch; continuing because CRC policy is warn\n");
#endif

    cache_prepare_for_execution(header.load_addr, header.size);
    uart_puts("PMOSBOOT EXEC ");
    uart_put_hex32(header.entry_addr);
    uart_puts("\n");
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

int uart_ramloader_probe_and_run(u32 soc_family)
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
    uart_puts(" PROBE_MS="); uart_put_hex32(CONFIG_UART_RAMLOADER_PROBE_TIMEOUT_MS);
    uart_puts("\n");

    timer_start(&probe_timer);
    while (!uart_rx_ready()) {
        if (timer_expired(&probe_timer, CONFIG_UART_RAMLOADER_PROBE_TIMEOUT_MS)) return 0;
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
        if (frame.magic != RAMLOADER_FRAME_MAGIC || frame.length == 0u ||
            frame.length > header.chunk_size || frame.length > RAMLOADER_MAX_CHUNK)
            return abort_to_boot("INVALID-FRAME");
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
        if (frame.sequence != expected_sequence || frame_crc != frame.crc32 ||
            frame.length > header.total_size - received) {
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
    if (image_crc != header.image_crc32) return abort_to_boot("IMAGE-CRC32-MISMATCH");
    if (!pmos_digest_equal(digest, header.image_sha256, 32u)) return abort_to_boot("IMAGE-SHA256-MISMATCH");

    uart_puts("PMOSRAM VERIFIED SHA256=");
    for (i = 0; i < 32u; i++) {
        uart_put_hex_nibble(digest[i] >> 4);
        uart_put_hex_nibble(digest[i]);
    }
    uart_puts("\nPMOSRAM EXEC "); uart_put_hex32(header.entry_addr); uart_puts("\n");
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
    (void)uart_ramloader_probe_and_run(soc_family);
    uart_puts("PMOSBOOT UART-DONE\n");
    boot_flash_kernel(payload_header_addr, fallback_entry);
}

