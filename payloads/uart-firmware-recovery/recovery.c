/*
 * postmerkOS pre-kernel UART firmware recovery payload, protocol v2.
 *
 * This flat MIPS program is loaded at 0x81000000 by LinuxLoader. It receives a
 * manifest-bound 16 MiB firmware image through acknowledged CRC-32 frames,
 * checks whole-object SHA-256, validates target compatibility and flash
 * geometry, then performs an explicitly challenged full-NOR rewrite.
 */

#include <postmerkos_uart_crypto.h>

typedef pmos_u8 u8;
typedef pmos_u32 u32;

#define UART_BASE 0x70100000u
#define UART_LSR 20u
#define UART_LSR_DR 1u
#define UART_LSR_THRE 0x20u
#define FULL_IMAGE_SIZE 0x01000000u
#define LOADER_REGION_SIZE 0x00040000u
#define KERNEL_OFFSET 0x00040000u
#define ROOTFS_OFFSET 0x00300000u
#define STAGING_BASE ((u8 *)0x81400000u)
#define MANIFEST_BASE (STAGING_BASE + FULL_IMAGE_SIZE)
#define MAX_MANIFEST (64u * 1024u)
#define MAX_CHUNK 4096u
#define PACKAGE_PROTOCOL_VERSION 2u
#define PACKAGE_HEADER_BYTES 124u
#define FRAME_HEADER_BYTES 20u
#define PKG_MAGIC0 0x534f4d50u /* PMOS */
#define PKG_MAGIC1 0x32474b50u /* PKG2 */
#define FRAME_MAGIC 0x32464b50u /* PKF2 */
#define OBJECT_IMAGE 1u
#define OBJECT_MANIFEST 2u
#define FLAG_FULL_FLASH 0x00000001u
#define FLAG_DRY_RUN 0x00000002u
#define FLAG_FORCE_UNTESTED 0x00000004u
#define KNOWN_FLAGS (FLAG_FULL_FLASH | FLAG_DRY_RUN | FLAG_FORCE_UNTESTED)
#define COUNT_HZ 208000000u
#define INTERBYTE_TIMEOUT_MS 3000u
#define PACKAGE_HEADER_TIMEOUT_MS 30000u
#define OBJECT_TRANSFER_TIMEOUT_MS (45u * 60u * 1000u)
#define CONFIRM_TIMEOUT_MS 60000u
#define SPI_STATUS_WIP 0x01u
#define SPI_STATUS_WEL 0x02u
#define SPI_STATUS_BP_MASK 0x3cu

#ifndef RECOVERY_SOC_FAMILY
#error RECOVERY_SOC_FAMILY must be 1 for Luton26 or 2 for Jaguar1
#endif

#if RECOVERY_SOC_FAMILY == 1
#define RECOVERY_SOC_NAME "luton26"
#define SPI_SW_MODE_ADDR 0x70000064u
#define RECOVERY_DESCRIPTOR "PMOSRECOVERY2;SOC=luton26;FAMILY=1;SPI=70000064;PROTO=2;END"
#elif RECOVERY_SOC_FAMILY == 2
#define RECOVERY_SOC_NAME "jaguar1"
#define SPI_SW_MODE_ADDR 0x70000068u
#define RECOVERY_DESCRIPTOR "PMOSRECOVERY2;SOC=jaguar1;FAMILY=2;SPI=70000068;PROTO=2;END"
#else
#error unsupported RECOVERY_SOC_FAMILY
#endif

#define SPI_SW_PIN_CTRL_MODE (1u << 13)
#define SPI_SW_SCK           (1u << 12)
#define SPI_SW_SCK_OE        (1u << 11)
#define SPI_SW_SDO           (1u << 10)
#define SPI_SW_SDO_OE        (1u << 9)
#define SPI_SW_CS_SHIFT      5u
#define SPI_SW_CS_OE_SHIFT   1u
#define SPI_SW_SDI           (1u << 0)
#define SPI_CS_ALL_HIGH      0x0fu
#define SPI_CS0_LOW          0x0eu
#define SPI_HALF_PERIOD_TICKS 32u

struct package_header {
    u32 magic0;
    u32 magic1;
    u32 version;
    u32 flags;
    u32 soc_family;
    u32 image_size;
    u32 manifest_size;
    u32 chunk_size;
    u32 image_crc32;
    u32 manifest_crc32;
    u8 image_sha256[32];
    u8 manifest_sha256[32];
    char target_model[16];
    u32 header_crc32;
};

struct frame_header {
    u32 magic;
    u32 object_id;
    u32 sequence;
    u32 length;
    u32 crc32;
};

struct pmos_timer {
    u32 last_count;
    u32 remainder_ticks;
    u32 elapsed_ms;
};

struct spi_device {
    u8 manufacturer;
    u8 memory_type;
    u8 capacity;
    u8 erase_opcode;
    u32 bytes;
    u32 erase_size;
    u32 page_size;
    u32 erase_timeout_ms;
    u32 program_timeout_ms;
    u8 flag_status_opcode;
    u8 flag_error_mask;
    u8 clear_flag_opcode;
};

static const char recovery_descriptor[] = RECOVERY_DESCRIPTOR;
static const struct spi_device supported_devices[] = {
    {0xc2u, 0x20u, 0x18u, 0xd8u, FULL_IMAGE_SIZE, 0x10000u, 256u, 8000u, 100u, 0u, 0u, 0u},
    {0xefu, 0x40u, 0x18u, 0xd8u, FULL_IMAGE_SIZE, 0x10000u, 256u, 8000u, 100u, 0u, 0u, 0u},
    {0x01u, 0x20u, 0x18u, 0xd8u, FULL_IMAGE_SIZE, 0x10000u, 256u, 8000u, 100u, 0u, 0u, 0u},
    {0x20u, 0xbau, 0x18u, 0xd8u, FULL_IMAGE_SIZE, 0x10000u, 256u, 8000u, 100u, 0x70u, 0x3au, 0x50u},
    {0xc8u, 0x40u, 0x18u, 0xd8u, FULL_IMAGE_SIZE, 0x10000u, 256u, 8000u, 100u, 0u, 0u, 0u}
};

static volatile u32 * const uart = (volatile u32 *)UART_BASE;
static volatile u32 * const spi_sw = (volatile u32 *)SPI_SW_MODE_ADDR;

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
    const u32 ticks_per_ms = COUNT_HZ / 1000u;
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

static int rx_ready(void)
{
    return (uart[UART_LSR / 4] & UART_LSR_DR) != 0;
}

/* The timeout covers the complete requested block. */
static int recv_exact(u8 *destination, u32 length, u32 milliseconds)
{
    struct pmos_timer timer;
    u32 received = 0u;
    timer_start(&timer);
    while (received < length) {
        while (!rx_ready()) {
            if (timer_expired(&timer, milliseconds)) return 0;
        }
        destination[received++] = (u8)(uart[0] & 0xffu);
        if (timer_expired(&timer, milliseconds) && received < length) return 0;
    }
    return 1;
}

static void putc_b(char c)
{
    if (c == '\n') putc_b('\r');
    while ((uart[UART_LSR / 4] & UART_LSR_THRE) == 0u) {}
    uart[0] = (u32)(u8)c;
}

static void puts_b(const char *text)
{
    while (*text) putc_b(*text++);
}

static void hex_nibble(u32 value)
{
    value &= 0xfu;
    putc_b((char)(value < 10u ? ('0' + value) : ('a' + value - 10u)));
}

static void hex8(u8 value)
{
    hex_nibble(value >> 4);
    hex_nibble(value);
}

static void hex32(u32 value)
{
    int shift;
    for (shift = 28; shift >= 0; shift -= 4) hex_nibble(value >> shift);
}

static u32 le32(const u8 *data)
{
    return (u32)data[0] | ((u32)data[1] << 8) |
           ((u32)data[2] << 16) | ((u32)data[3] << 24);
}

static int bytes_equal(const u8 *left, const u8 *right, u32 length)
{
    u32 i;
    for (i = 0; i < length; i++) if (left[i] != right[i]) return 0;
    return 1;
}

static int text_equal(const char *left, const char *right)
{
    while (*left && *right && *left == *right) { left++; right++; }
    return *left == 0 && *right == 0;
}

static int model_allowed_for_payload(const char *model)
{
#if RECOVERY_SOC_FAMILY == 1
    return text_equal(model, "MS22") || text_equal(model, "MS22P") ||
           text_equal(model, "MS220-8") || text_equal(model, "MS220-8P") ||
           text_equal(model, "MS220-24") || text_equal(model, "MS220-24P");
#else
    return text_equal(model, "MS320-24") || text_equal(model, "MS320-24P") ||
           text_equal(model, "MS220-48") || text_equal(model, "MS220-48P") ||
           text_equal(model, "MS220-48LP") || text_equal(model, "MS220-48FP") ||
           text_equal(model, "MS320-48") || text_equal(model, "MS320-48P") ||
           text_equal(model, "MS320-48LP") || text_equal(model, "MS320-48FP") ||
           text_equal(model, "MS42") || text_equal(model, "MS42P");
#endif
}

static void parse_package_header(struct package_header *header, const u8 *raw)
{
    u32 i;
    header->magic0 = le32(raw);
    header->magic1 = le32(raw + 4);
    header->version = le32(raw + 8);
    header->flags = le32(raw + 12);
    header->soc_family = le32(raw + 16);
    header->image_size = le32(raw + 20);
    header->manifest_size = le32(raw + 24);
    header->chunk_size = le32(raw + 28);
    header->image_crc32 = le32(raw + 32);
    header->manifest_crc32 = le32(raw + 36);
    for (i = 0; i < 32u; i++) header->image_sha256[i] = raw[40u + i];
    for (i = 0; i < 32u; i++) header->manifest_sha256[i] = raw[72u + i];
    for (i = 0; i < 15u; i++) header->target_model[i] = (char)raw[104u + i];
    header->target_model[15] = 0;
    header->header_crc32 = le32(raw + 120);
}

static int validate_package_header(const struct package_header *header, const u8 *raw)
{
    if (header->magic0 != PKG_MAGIC0 || header->magic1 != PKG_MAGIC1) return 0;
    if (header->version != PACKAGE_PROTOCOL_VERSION) return 0;
    if ((header->flags & ~KNOWN_FLAGS) != 0u || (header->flags & FLAG_FULL_FLASH) == 0u) return 0;
    if (header->soc_family != RECOVERY_SOC_FAMILY) return 0;
    if (header->image_size != FULL_IMAGE_SIZE || header->manifest_size == 0u ||
        header->manifest_size > MAX_MANIFEST) return 0;
    if (header->chunk_size < 64u || header->chunk_size > MAX_CHUNK) return 0;
    if (!model_allowed_for_payload(header->target_model)) return 0;
    if (header->header_crc32 != pmos_crc32(raw, PACKAGE_HEADER_BYTES - 4u)) return 0;
    return 1;
}

static void parse_frame_header(struct frame_header *frame, const u8 *raw)
{
    frame->magic = le32(raw);
    frame->object_id = le32(raw + 4);
    frame->sequence = le32(raw + 8);
    frame->length = le32(raw + 12);
    frame->crc32 = le32(raw + 16);
}

static void send_frame_status(const char *status, u32 object_id, u32 sequence)
{
    puts_b("PMOSPKG "); puts_b(status); putc_b(' '); hex32(object_id);
    putc_b(' '); hex32(sequence); puts_b("\n");
}

static int receive_object(u32 object_id, u8 *destination, u32 total_size,
                          u32 chunk_size, u32 expected_crc, const u8 expected_sha[32])
{
    u8 frame_raw[FRAME_HEADER_BYTES];
    u8 chunk[MAX_CHUNK];
    u8 digest[32];
    struct frame_header frame;
    pmos_sha256_ctx sha;
    u32 received = 0u;
    u32 sequence = 0u;
    u32 previous_length = 0u;
    u32 previous_crc = 0u;
    u32 frame_crc;
    u32 crc = 0xffffffffu;
    u32 i;
    struct pmos_timer transfer_timer;

    pmos_sha256_init(&sha);
    timer_start(&transfer_timer);
    while (received < total_size) {
        if (timer_expired(&transfer_timer, OBJECT_TRANSFER_TIMEOUT_MS)) {
            puts_b("PMOSREC RESULT ERROR OBJECT-TRANSFER-TIMEOUT\n"); return -1;
        }
        if (!recv_exact(frame_raw, FRAME_HEADER_BYTES, INTERBYTE_TIMEOUT_MS)) {
            puts_b("PMOSREC RESULT ERROR FRAME-HEADER-TIMEOUT\n"); return -1;
        }
        parse_frame_header(&frame, frame_raw);
        if (frame.magic != FRAME_MAGIC || frame.object_id != object_id ||
            frame.length == 0u || frame.length > chunk_size || frame.length > MAX_CHUNK) {
            puts_b("PMOSREC RESULT ERROR INVALID-FRAME\n"); return -1;
        }
        if (!recv_exact(chunk, frame.length, INTERBYTE_TIMEOUT_MS)) {
            puts_b("PMOSREC RESULT ERROR FRAME-DATA-TIMEOUT\n"); return -1;
        }
        frame_crc = pmos_crc32(chunk, frame.length);
        if (sequence != 0u && frame.sequence == sequence - 1u) {
            if (frame.length == previous_length && frame.crc32 == previous_crc &&
                frame_crc == frame.crc32)
                send_frame_status("ACK", object_id, frame.sequence);
            else
                send_frame_status("NACK", object_id, sequence);
            continue;
        }
        if (frame.sequence != sequence || frame_crc != frame.crc32 ||
            frame.length > total_size - received) {
            send_frame_status("NACK", object_id, sequence);
            continue;
        }
        for (i = 0; i < frame.length; i++) destination[received + i] = chunk[i];
        crc = pmos_crc32_update(crc, chunk, frame.length);
        pmos_sha256_update(&sha, chunk, frame.length);
        received += frame.length;
        previous_length = frame.length;
        previous_crc = frame.crc32;
        send_frame_status("ACK", object_id, sequence);
        sequence++;
    }
    crc = ~crc;
    pmos_sha256_final(&sha, digest);
    if (crc != expected_crc) {
        puts_b("PMOSREC RESULT ERROR OBJECT-CRC32-MISMATCH\n"); return -1;
    }
    if (!pmos_digest_equal(digest, expected_sha, 32u)) {
        puts_b("PMOSREC RESULT ERROR OBJECT-SHA256-MISMATCH\n"); return -1;
    }
    puts_b("PMOSPKG OBJECT-VERIFIED "); hex32(object_id); puts_b("\n");
    return 0;
}

static u32 json_skip_ws(const u8 *data, u32 length, u32 position)
{
    while (position < length && (data[position] == ' ' || data[position] == '\t' ||
           data[position] == '\r' || data[position] == '\n')) position++;
    return position;
}

static int json_find_value(const u8 *data, u32 length, const char *key, u32 *position)
{
    u32 i = 0u, k, p, object_depth = 0u, array_depth = 0u;
    u32 direct_object_depth = 0u;
    int in_string = 0, escaped = 0;

    p = json_skip_ws(data, length, 0u);
    if (p < length && data[p] == '{') direct_object_depth = 1u;

    while (i < length) {
        u8 c = data[i];
        if (in_string) {
            if (escaped) escaped = 0;
            else if (c == '\\') escaped = 1;
            else if (c == '"') in_string = 0;
            i++;
            continue;
        }
        if (c == '{') { object_depth++; i++; continue; }
        if (c == '}') { if (object_depth != 0u) object_depth--; i++; continue; }
        if (c == '[') { array_depth++; i++; continue; }
        if (c == ']') { if (array_depth != 0u) array_depth--; i++; continue; }
        if (c != '"') { i++; continue; }

        if (object_depth != direct_object_depth || array_depth != 0u) {
            in_string = 1;
            i++;
            continue;
        }

        p = i + 1u;
        k = 0u;
        while (key[k] && p < length && data[p] == (u8)key[k]) { p++; k++; }
        if (!key[k] && p < length && data[p] == '"') {
            p = json_skip_ws(data, length, p + 1u);
            if (p < length && data[p] == ':') {
                p = json_skip_ws(data, length, p + 1u);
                if (p >= length) return 0;
                *position = p;
                return 1;
            }
        }

        in_string = 1;
        i++;
    }
    return 0;
}

static int json_string_value(const u8 *data, u32 length, const char *key,
                             char *output, u32 output_size)
{
    u32 p, used;
    if (!json_find_value(data, length, key, &p) || data[p++] != '"') return 0;
    used = 0u;
    while (p < length && data[p] != '"') {
        if (data[p] == '\\' || used + 1u >= output_size) return 0;
        output[used++] = (char)data[p++];
    }
    if (p >= length || data[p] != '"') return 0;
    output[used] = 0;
    p = json_skip_ws(data, length, p + 1u);
    if (p < length && data[p] != ',' && data[p] != '}' && data[p] != ']') return 0;
    return 1;
}

static int json_u32_value(const u8 *data, u32 length, const char *key, u32 *output)
{
    u32 p, value = 0u, digits = 0u;
    if (!json_find_value(data, length, key, &p)) return 0;
    while (p < length && data[p] >= '0' && data[p] <= '9') {
        u32 next = value * 10u + (u32)(data[p] - '0');
        if (next < value) return 0;
        value = next;
        p++; digits++;
    }
    if (digits == 0u) return 0;
    p = json_skip_ws(data, length, p);
    if (p < length && data[p] != ',' && data[p] != '}') return 0;
    *output = value;
    return 1;
}

static int json_true_value(const u8 *data, u32 length, const char *key)
{
    u32 p;
    if (!json_find_value(data, length, key, &p) || p + 4u > length) return 0;
    if (data[p] != 't' || data[p + 1u] != 'r' ||
        data[p + 2u] != 'u' || data[p + 3u] != 'e') return 0;
    p = json_skip_ws(data, length, p + 4u);
    return p == length || data[p] == ',' || data[p] == '}';
}

static int json_array_contains_string(const u8 *data, u32 length,
                                      const char *key, const char *expected)
{
    u32 p, used;
    char value[48];
    if (!json_find_value(data, length, key, &p) || data[p++] != '[') return 0;
    for (;;) {
        p = json_skip_ws(data, length, p);
        if (p >= length) return 0;
        if (data[p] == ']') return 0;
        if (data[p++] != '"') return 0;
        used = 0u;
        while (p < length && data[p] != '"') {
            if (data[p] == '\\' || used + 1u >= sizeof(value)) return 0;
            value[used++] = (char)data[p++];
        }
        if (p >= length || data[p++] != '"') return 0;
        value[used] = 0;
        if (text_equal(value, expected)) return 1;
        p = json_skip_ws(data, length, p);
        if (p >= length || (data[p] != ',' && data[p] != ']')) return 0;
        if (data[p] == ']') return 0;
        p++;
    }
}

static int json_object_value(const u8 *data, u32 length, const char *key,
                             const u8 **object, u32 *object_length)
{
    u32 p, start, depth = 0u;
    int in_string = 0, escaped = 0;
    if (!json_find_value(data, length, key, &p) || data[p] != '{') return 0;
    start = p + 1u;
    for (; p < length; p++) {
        u8 c = data[p];
        if (in_string) {
            if (escaped) escaped = 0;
            else if (c == '\\') escaped = 1;
            else if (c == '"') in_string = 0;
            continue;
        }
        if (c == '"') { in_string = 1; continue; }
        if (c == '{') depth++;
        else if (c == '}') {
            if (depth == 0u) return 0;
            depth--;
            if (depth == 0u) {
                *object = data + start;
                *object_length = p - start;
                return 1;
            }
        }
    }
    return 0;
}

static int contains_bytes(const u8 *haystack, u32 haystack_size,
                          const u8 *needle, u32 needle_size)
{
    u32 i;
    if (needle_size == 0u || needle_size > haystack_size) return 0;
    for (i = 0; i <= haystack_size - needle_size; i++)
        if (bytes_equal(haystack + i, needle, needle_size)) return 1;
    return 0;
}

static void digest_to_hex(const u8 digest[32], u8 output[64])
{
    static const char digits[] = "0123456789abcdef";
    u32 i;
    for (i = 0; i < 32u; i++) {
        output[i * 2u] = (u8)digits[(digest[i] >> 4) & 0xfu];
        output[i * 2u + 1u] = (u8)digits[digest[i] & 0xfu];
    }
}

static int validate_manifest_and_image(const struct package_header *header)
{
    char family[16];
    char status[40];
    char artifact_sha[65];
    char loader_sha[65];
    char boot_chain[48];
    u8 image_digest_hex[64];
    u8 loader_digest[32];
    u8 loader_digest_hex[64];
    pmos_sha256_ctx loader_sha_ctx;
    const u8 *artifact_object, *models_object, *recovery_object, *uart_object;
    const u8 *firmware_object, *geometry_object, *payloads_object, *payload_object;
    u32 artifact_length, models_length, recovery_length, uart_length;
    u32 firmware_length, geometry_length, payloads_length, payload_length;
    u32 artifact_bytes, protocol_version, full_image_bytes;
    u32 flash_bytes, erase_bytes, page_bytes, address_bytes, family_id, spi_address;
    u32 payload_bytes;
    char payload_sha[65];
    static const u8 loader_marker[] = "PMOSRAM READY 2";
    static const u8 spim_magic[] = "SPIM";
    static const u8 squashfs_magic[] = "hsqs";

    if (!json_string_value(MANIFEST_BASE, header->manifest_size, "target_family", family, sizeof(family)) ||
        !text_equal(family, "vcore3")) {
        puts_b("PMOSREC RESULT ERROR MANIFEST-TARGET-FAMILY\n"); return -1;
    }
    if (!json_object_value(MANIFEST_BASE, header->manifest_size, "models", &models_object, &models_length) ||
        !json_string_value(models_object, models_length, header->target_model, status, sizeof(status))) {
        puts_b("PMOSREC RESULT ERROR MANIFEST-MODEL-MISSING\n"); return -1;
    }
    if (text_equal(status, "known-incompatible")) {
        puts_b("PMOSREC RESULT ERROR MODEL-INCOMPATIBLE\n"); return -1;
    }
    if (text_equal(status, "untested") && (header->flags & FLAG_FORCE_UNTESTED) == 0u) {
        puts_b("PMOSREC RESULT ERROR MODEL-UNTESTED-REQUIRES-FORCE\n"); return -1;
    }
    if (!text_equal(status, "validated") &&
        !text_equal(status, "confirmed") && !text_equal(status, "untested")) {
        puts_b("PMOSREC RESULT ERROR MODEL-STATUS-UNSUPPORTED\n"); return -1;
    }

    if (!json_object_value(MANIFEST_BASE, header->manifest_size, "artifact", &artifact_object, &artifact_length) ||
        !json_u32_value(artifact_object, artifact_length, "bytes", &artifact_bytes) ||
        artifact_bytes != FULL_IMAGE_SIZE ||
        !json_string_value(artifact_object, artifact_length, "sha256", artifact_sha, sizeof(artifact_sha)) ||
        !json_string_value(artifact_object, artifact_length, "boot_chain", boot_chain, sizeof(boot_chain)) ||
        !text_equal(boot_chain, "vcoreiii-linuxloader-spim-v2")) {
        puts_b("PMOSREC RESULT ERROR MANIFEST-ARTIFACT\n"); return -1;
    }
    digest_to_hex(header->image_sha256, image_digest_hex);
    if (!bytes_equal((const u8 *)artifact_sha, image_digest_hex, 64u) || artifact_sha[64] != 0) {
        puts_b("PMOSREC RESULT ERROR MANIFEST-IMAGE-DIGEST\n"); return -1;
    }

    if (!json_object_value(MANIFEST_BASE, header->manifest_size, "recovery", &recovery_object, &recovery_length) ||
        !json_object_value(recovery_object, recovery_length, "uart_ramloader", &uart_object, &uart_length) ||
        !json_true_value(uart_object, uart_length, "enabled") ||
        !json_u32_value(uart_object, uart_length, "protocol_version", &protocol_version) ||
        protocol_version != PACKAGE_PROTOCOL_VERSION ||
        !json_string_value(uart_object, uart_length, "loader_sha256", loader_sha, sizeof(loader_sha))) {
        puts_b("PMOSREC RESULT ERROR MANIFEST-RECOVERY-CAPABILITY\n"); return -1;
    }

    if (!json_object_value(recovery_object, recovery_length, "uart_firmware", &firmware_object, &firmware_length) ||
        !json_true_value(firmware_object, firmware_length, "enabled") ||
        !json_u32_value(firmware_object, firmware_length, "protocol_version", &protocol_version) ||
        protocol_version != PACKAGE_PROTOCOL_VERSION ||
        !json_u32_value(firmware_object, firmware_length, "full_image_bytes", &full_image_bytes) ||
        full_image_bytes != FULL_IMAGE_SIZE ||
        !json_object_value(firmware_object, firmware_length, "flash_geometry", &geometry_object, &geometry_length) ||
        !json_u32_value(geometry_object, geometry_length, "bytes", &flash_bytes) ||
        !json_u32_value(geometry_object, geometry_length, "erase_bytes", &erase_bytes) ||
        !json_u32_value(geometry_object, geometry_length, "page_bytes", &page_bytes) ||
        !json_u32_value(geometry_object, geometry_length, "address_bytes", &address_bytes) ||
        flash_bytes != FULL_IMAGE_SIZE || erase_bytes != 0x10000u ||
        page_bytes != 256u || address_bytes != 3u ||
        !json_object_value(firmware_object, firmware_length, "payloads", &payloads_object, &payloads_length) ||
        !json_object_value(payloads_object, payloads_length, RECOVERY_SOC_NAME, &payload_object, &payload_length) ||
        !json_u32_value(payload_object, payload_length, "soc_family_id", &family_id) ||
        family_id != RECOVERY_SOC_FAMILY ||
        !json_u32_value(payload_object, payload_length, "spi_software_mode_address", &spi_address) ||
        spi_address != SPI_SW_MODE_ADDR ||
        !json_u32_value(payload_object, payload_length, "bytes", &payload_bytes) || payload_bytes == 0u ||
        !json_string_value(payload_object, payload_length, "sha256", payload_sha, sizeof(payload_sha)) ||
        payload_sha[64] != 0 ||
        !json_array_contains_string(payload_object, payload_length, "accepted_models", header->target_model)) {
        puts_b("PMOSREC RESULT ERROR MANIFEST-FIRMWARE-RECOVERY\n"); return -1;
    }
    if (!json_array_contains_string(uart_object, uart_length, "supported_soc_families", RECOVERY_SOC_NAME)) {
        puts_b("PMOSREC RESULT ERROR MANIFEST-LOADER-SOC\n"); return -1;
    }
    if ((header->flags & FLAG_DRY_RUN) != 0u) {
        if (!json_array_contains_string(firmware_object, firmware_length,
                                        "operations", "dry-run")) {
            puts_b("PMOSREC RESULT ERROR MANIFEST-PROTOCOL-CONTRACT\n"); return -1;
        }
    } else if (!json_array_contains_string(firmware_object, firmware_length,
                                           "operations", "flash")) {
        puts_b("PMOSREC RESULT ERROR MANIFEST-PROTOCOL-CONTRACT\n"); return -1;
    }
    if (!json_array_contains_string(firmware_object, firmware_length,
                                    "transport_integrity", "frame-crc32")) {
        puts_b("PMOSREC RESULT ERROR MANIFEST-PROTOCOL-CONTRACT\n"); return -1;
    }
    if (!json_array_contains_string(firmware_object, firmware_length,
                                    "transport_integrity", "object-crc32")) {
        puts_b("PMOSREC RESULT ERROR MANIFEST-PROTOCOL-CONTRACT\n"); return -1;
    }
    if (!json_array_contains_string(firmware_object, firmware_length,
                                    "transport_integrity", "object-sha256")) {
        puts_b("PMOSREC RESULT ERROR MANIFEST-PROTOCOL-CONTRACT\n"); return -1;
    }

    if (!bytes_equal(STAGING_BASE + KERNEL_OFFSET, spim_magic, 4u) ||
        !bytes_equal(STAGING_BASE + ROOTFS_OFFSET, squashfs_magic, 4u)) {
        puts_b("PMOSREC RESULT ERROR IMAGE-LAYOUT\n"); return -1;
    }
    if (!contains_bytes(STAGING_BASE, LOADER_REGION_SIZE, loader_marker,
                        (u32)(sizeof(loader_marker) - 1u))) {
        puts_b("PMOSREC RESULT ERROR LOADER-CAPABILITY-MISSING\n"); return -1;
    }
    pmos_sha256_init(&loader_sha_ctx);
    pmos_sha256_update(&loader_sha_ctx, STAGING_BASE, LOADER_REGION_SIZE);
    pmos_sha256_final(&loader_sha_ctx, loader_digest);
    digest_to_hex(loader_digest, loader_digest_hex);
    if (!bytes_equal((const u8 *)loader_sha, loader_digest_hex, 64u) || loader_sha[64] != 0) {
        puts_b("PMOSREC RESULT ERROR MANIFEST-LOADER-DIGEST\n"); return -1;
    }
    return 0;
}

static int validate_flash_manifest(const struct package_header *header,
                                   const struct spi_device *device, const u8 id[3])
{
    static const char digits[] = "0123456789abcdef";
    char jedec[7];
    const u8 *recovery_object, *firmware_object, *geometry_object;
    u32 recovery_length, firmware_length, geometry_length;
    u32 flash_bytes, erase_bytes, page_bytes, address_bytes;
    u32 i;
    for (i = 0u; i < 3u; i++) {
        jedec[i * 2u] = digits[(id[i] >> 4) & 0xfu];
        jedec[i * 2u + 1u] = digits[id[i] & 0xfu];
    }
    jedec[6] = 0;
    if (!json_object_value(MANIFEST_BASE, header->manifest_size, "recovery", &recovery_object, &recovery_length) ||
        !json_object_value(recovery_object, recovery_length, "uart_firmware", &firmware_object, &firmware_length) ||
        !json_object_value(firmware_object, firmware_length, "flash_geometry", &geometry_object, &geometry_length) ||
        !json_u32_value(geometry_object, geometry_length, "bytes", &flash_bytes) ||
        !json_u32_value(geometry_object, geometry_length, "erase_bytes", &erase_bytes) ||
        !json_u32_value(geometry_object, geometry_length, "page_bytes", &page_bytes) ||
        !json_u32_value(geometry_object, geometry_length, "address_bytes", &address_bytes) ||
        flash_bytes != device->bytes || erase_bytes != device->erase_size ||
        page_bytes != device->page_size || address_bytes != 3u ||
        !json_array_contains_string(firmware_object, firmware_length, "accepted_jedec_ids", jedec)) {
        puts_b("PMOSREC RESULT ERROR MANIFEST-FLASH-CONSTRAINTS\n"); return -1;
    }
    return 0;
}

static void spi_delay(void)
{
    u32 start = cp0_count();
    while ((u32)(cp0_count() - start) < SPI_HALF_PERIOD_TICKS) {}
}

static void spi_drive(u32 chip_select, u32 clock, u32 data_out)
{
    u32 value = SPI_SW_PIN_CTRL_MODE | SPI_SW_SCK_OE | SPI_SW_SDO_OE |
                (1u << SPI_SW_CS_OE_SHIFT) | (chip_select << SPI_SW_CS_SHIFT);
    if (clock) value |= SPI_SW_SCK;
    if (data_out) value |= SPI_SW_SDO;
    *spi_sw = value;
    spi_delay();
}

static void spi_deselect(void)
{
    spi_drive(SPI_CS_ALL_HIGH, 0u, 0u);
    spi_drive(SPI_CS_ALL_HIGH, 1u, 0u);
    spi_drive(SPI_CS_ALL_HIGH, 0u, 0u);
}

static void spi_select(void)
{
    spi_drive(SPI_CS_ALL_HIGH, 0u, 0u);
    spi_drive(SPI_CS0_LOW, 0u, 0u);
}

static u8 spi_xfer(u8 output)
{
    u8 input = 0u;
    int bit;
    for (bit = 7; bit >= 0; bit--) {
        spi_drive(SPI_CS0_LOW, 0u, (output >> bit) & 1u);
        spi_drive(SPI_CS0_LOW, 1u, (output >> bit) & 1u);
        if ((*spi_sw & SPI_SW_SDI) != 0u) input |= (u8)(1u << bit);
        spi_drive(SPI_CS0_LOW, 0u, (output >> bit) & 1u);
    }
    return input;
}

static u8 spi_read_status(void);

static void spi_command(u8 command)
{
    spi_select(); spi_xfer(command); spi_deselect();
}

static u8 spi_read_register(u8 command)
{
    u8 value;
    spi_select(); spi_xfer(command); value = spi_xfer(0xffu); spi_deselect();
    return value;
}

static int spi_clear_and_check_errors(const struct spi_device *device)
{
    u8 flags;
    if (device->flag_status_opcode == 0u || device->flag_error_mask == 0u) return 0;
    if (device->clear_flag_opcode != 0u) spi_command(device->clear_flag_opcode);
    flags = spi_read_register(device->flag_status_opcode);
    return (flags & device->flag_error_mask) == 0u ? 0 : -1;
}

static int spi_check_completion(const struct spi_device *device, u32 address,
                                const char *phase)
{
    u8 status = spi_read_status();
    if ((status & SPI_STATUS_WIP) != 0u || (status & SPI_STATUS_WEL) != 0u) {
        puts_b("PMOSREC RESULT ERROR FLASH-STATUS-"); puts_b(phase);
        puts_b(" AT="); hex32(address); puts_b(" STATUS="); hex8(status); puts_b("\n");
        return -1;
    }
    if (device->flag_status_opcode != 0u &&
        (spi_read_register(device->flag_status_opcode) & device->flag_error_mask) != 0u) {
        puts_b("PMOSREC RESULT ERROR FLASH-FLAG-"); puts_b(phase);
        puts_b(" AT="); hex32(address); puts_b("\n");
        return -1;
    }
    return 0;
}

static u8 spi_read_status(void)
{
    u8 status;
    spi_select(); spi_xfer(0x05u); status = spi_xfer(0xffu); spi_deselect();
    return status;
}

static int spi_wait_ready(u32 timeout_ms)
{
    struct pmos_timer timer;
    timer_start(&timer);
    while ((spi_read_status() & SPI_STATUS_WIP) != 0u) {
        if (timer_expired(&timer, timeout_ms)) return -1;
    }
    return 0;
}

static int spi_write_enable(void)
{
    spi_command(0x06u);
    return (spi_read_status() & SPI_STATUS_WEL) != 0u ? 0 : -1;
}

static void spi_read_id(u8 id[3])
{
    spi_select(); spi_xfer(0x9fu);
    id[0] = spi_xfer(0xffu); id[1] = spi_xfer(0xffu); id[2] = spi_xfer(0xffu);
    spi_deselect();
}

static const struct spi_device *find_spi_device(const u8 id[3])
{
    u32 i;
    for (i = 0; i < (u32)(sizeof(supported_devices) / sizeof(supported_devices[0])); i++) {
        if (supported_devices[i].manufacturer == id[0] &&
            supported_devices[i].memory_type == id[1] &&
            supported_devices[i].capacity == id[2]) return &supported_devices[i];
    }
    return (const struct spi_device *)0;
}

static u8 spi_read_byte(u32 address)
{
    u8 value;
    spi_select(); spi_xfer(0x03u);
    spi_xfer((u8)(address >> 16)); spi_xfer((u8)(address >> 8)); spi_xfer((u8)address);
    value = spi_xfer(0xffu); spi_deselect();
    return value;
}

static int spi_preflight(const struct spi_device **device_out, u8 id[3])
{
    u8 status;
    const struct spi_device *device;
    spi_deselect();
    spi_read_id(id);
    puts_b("PMOSREC FLASH-ID "); hex8(id[0]); hex8(id[1]); hex8(id[2]); puts_b("\n");
    device = find_spi_device(id);
    if (!device || device->bytes != FULL_IMAGE_SIZE) {
        puts_b("PMOSREC RESULT ERROR UNSUPPORTED-JEDEC\n"); return -1;
    }
    status = spi_read_status();
    if ((status & SPI_STATUS_WIP) != 0u && spi_wait_ready(device->erase_timeout_ms) != 0) {
        puts_b("PMOSREC RESULT ERROR FLASH-BUSY-TIMEOUT\n"); return -1;
    }
    status = spi_read_status();
    if ((status & SPI_STATUS_BP_MASK) != 0u) {
        puts_b("PMOSREC RESULT ERROR FLASH-PROTECTED\n"); return -1;
    }
    if (spi_clear_and_check_errors(device) != 0) {
        puts_b("PMOSREC RESULT ERROR FLASH-ERROR-STATUS\n"); return -1;
    }
    *device_out = device;
    return 0;
}

static int spi_erase_full(const struct spi_device *device)
{
    u32 address;
    puts_b("PMOSREC PROGRESS ERASE-BEGIN\n");
    for (address = 0u; address < FULL_IMAGE_SIZE; address += device->erase_size) {
        if (spi_write_enable() != 0) {
            puts_b("PMOSREC RESULT ERROR WRITE-ENABLE-ERASE\n"); return -1;
        }
        spi_select(); spi_xfer(device->erase_opcode);
        spi_xfer((u8)(address >> 16)); spi_xfer((u8)(address >> 8)); spi_xfer((u8)address);
        spi_deselect();
        if (spi_wait_ready(device->erase_timeout_ms) != 0) {
            puts_b("PMOSREC RESULT ERROR ERASE-TIMEOUT AT="); hex32(address); puts_b("\n"); return -1;
        }
        if (spi_check_completion(device, address, "ERASE") != 0) return -1;
        if (spi_read_byte(address) != 0xffu ||
            spi_read_byte(address + device->erase_size - 1u) != 0xffu) {
            puts_b("PMOSREC RESULT ERROR ERASE-VERIFY AT="); hex32(address); puts_b("\n"); return -1;
        }
        if ((address & 0x000fffffu) == 0u) {
            puts_b("PMOSREC PROGRESS ERASE "); hex32(address); puts_b("\n");
        }
    }
    puts_b("PMOSREC PROGRESS ERASE-END\n");
    return 0;
}

static int spi_program_full(const struct spi_device *device)
{
    u32 address, offset;
    puts_b("PMOSREC PROGRESS PROGRAM-BEGIN\n");
    for (address = 0u; address < FULL_IMAGE_SIZE; address += device->page_size) {
        if (spi_write_enable() != 0) {
            puts_b("PMOSREC RESULT ERROR WRITE-ENABLE-PROGRAM\n"); return -1;
        }
        spi_select(); spi_xfer(0x02u);
        spi_xfer((u8)(address >> 16)); spi_xfer((u8)(address >> 8)); spi_xfer((u8)address);
        for (offset = 0u; offset < device->page_size; offset++)
            spi_xfer(STAGING_BASE[address + offset]);
        spi_deselect();
        if (spi_wait_ready(device->program_timeout_ms) != 0) {
            puts_b("PMOSREC RESULT ERROR PROGRAM-TIMEOUT AT="); hex32(address); puts_b("\n"); return -1;
        }
        if (spi_check_completion(device, address, "PROGRAM") != 0) return -1;
        if ((address & 0x000fffffu) == 0u) {
            puts_b("PMOSREC PROGRESS PROGRAM "); hex32(address); puts_b("\n");
        }
    }
    puts_b("PMOSREC PROGRESS PROGRAM-END\n");
    return 0;
}

static int spi_verify_full(const struct spi_device *device)
{
    u32 address, offset;
    u8 value;
    puts_b("PMOSREC PROGRESS VERIFY-BEGIN\n");
    for (address = 0u; address < FULL_IMAGE_SIZE; address += device->erase_size) {
        spi_select(); spi_xfer(0x03u);
        spi_xfer((u8)(address >> 16)); spi_xfer((u8)(address >> 8)); spi_xfer((u8)address);
        for (offset = 0u; offset < device->erase_size; offset++) {
            value = spi_xfer(0xffu);
            if (value != STAGING_BASE[address + offset]) {
                spi_deselect();
                puts_b("PMOSREC RESULT ERROR READBACK-MISMATCH AT="); hex32(address + offset); puts_b("\n");
                return -1;
            }
        }
        spi_deselect();
        puts_b("PMOSREC PROGRESS VERIFY "); hex32(address); puts_b("\n");
    }
    puts_b("PMOSREC PROGRESS VERIFY-END\n");
    return 0;
}

static int read_confirmation(u32 nonce)
{
    char line[48];
    char expected[32];
    static const char prefix[] = "ERASEFLASH ";
    struct pmos_timer timer;
    u32 used = 0u, i;
    u8 value;

    for (i = 0u; prefix[i]; i++) expected[i] = prefix[i];
    for (used = 0u; used < 8u; used++) {
        u32 shift = 28u - used * 4u;
        u32 nibble = (nonce >> shift) & 0xfu;
        expected[i + used] = (char)(nibble < 10u ? '0' + nibble : 'a' + nibble - 10u);
    }
    expected[i + 8u] = 0;
    used = 0u;
    timer_start(&timer);
    while (used + 1u < sizeof(line)) {
        while (!rx_ready()) {
            if (timer_expired(&timer, CONFIRM_TIMEOUT_MS)) return -1;
        }
        value = (u8)(uart[0] & 0xffu);
        if (timer_expired(&timer, CONFIRM_TIMEOUT_MS)) return -1;
        if (value == '\r' || value == '\n') {
            if (used == 0u) continue;
            break;
        }
        line[used++] = (char)value;
    }
    line[used] = 0;
    return text_equal(line, expected) ? 0 : -1;
}

static void halt(void)
{
    for (;;) __asm__ __volatile__("wait");
}

void recovery_main(void)
{
    u8 raw[PACKAGE_HEADER_BYTES];
    u8 id[3];
    struct package_header header;
    const struct spi_device *device = (const struct spi_device *)0;
    u32 nonce;

    puts_b("PMOSREC READY 2 SOC=" RECOVERY_SOC_NAME " FAMILY=");
    hex32(RECOVERY_SOC_FAMILY); puts_b(" SPI="); hex32(SPI_SW_MODE_ADDR);
    puts_b(" MAX_MANIFEST="); hex32(MAX_MANIFEST); puts_b("\n");
    puts_b("PMOSREC DESCRIPTOR "); puts_b(recovery_descriptor); puts_b("\n");

    if (!recv_exact(raw, PACKAGE_HEADER_BYTES, PACKAGE_HEADER_TIMEOUT_MS)) {
        puts_b("PMOSREC RESULT ERROR PACKAGE-HEADER-TIMEOUT\n"); halt();
    }
    parse_package_header(&header, raw);
    if (!validate_package_header(&header, raw)) {
        puts_b("PMOSREC RESULT ERROR INVALID-PACKAGE-HEADER\n"); halt();
    }
    puts_b("PMOSPKG HEADER-ACK MODEL="); puts_b(header.target_model);
    puts_b(" FLAGS="); hex32(header.flags); puts_b("\n");

    if (receive_object(OBJECT_IMAGE, STAGING_BASE, header.image_size, header.chunk_size,
                       header.image_crc32, header.image_sha256) != 0) halt();
    if (receive_object(OBJECT_MANIFEST, MANIFEST_BASE, header.manifest_size, header.chunk_size,
                       header.manifest_crc32, header.manifest_sha256) != 0) halt();
    if (validate_manifest_and_image(&header) != 0) halt();
    if (spi_preflight(&device, id) != 0) halt();
    if (validate_flash_manifest(&header, device, id) != 0) halt();

    puts_b("PMOSPKG VERIFIED MODEL="); puts_b(header.target_model); puts_b("\n");
    if ((header.flags & FLAG_DRY_RUN) != 0u) {
        puts_b("PMOSREC RESULT DRY-RUN-OK\n"); halt();
    }

    nonce = header.image_crc32 ^ header.manifest_crc32 ^ cp0_count() ^
            ((u32)id[0] << 16) ^ ((u32)id[1] << 8) ^ (u32)id[2];
    puts_b("PMOSREC ERASE-CHALLENGE "); hex32(nonce); puts_b("\n");
    if (read_confirmation(nonce) != 0) {
        puts_b("PMOSREC RESULT ABORT CONFIRMATION\n"); halt();
    }
    puts_b("PMOSREC CONFIRMATION-ACK\n");

    if (spi_erase_full(device) != 0 || spi_program_full(device) != 0 ||
        spi_verify_full(device) != 0) halt();
    puts_b("PMOSREC RESULT SUCCESS\n");
    halt();
}
