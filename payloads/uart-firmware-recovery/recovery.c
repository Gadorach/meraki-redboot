/*
 * postmerkOS pre-kernel UART firmware recovery payload, protocol v3.
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
#define UART_LSR_ERRORS 0x1eu
#define UART_LSR_THRE 0x20u
#define FULL_IMAGE_SIZE 0x01000000u
#define LOADER_REGION_SIZE 0x00040000u
#define KERNEL_OFFSET 0x00040000u
#define ROOTFS_OFFSET 0x00300000u
#define STAGING_BASE ((u8 *)0x81400000u)
#define MANIFEST_BASE (STAGING_BASE + FULL_IMAGE_SIZE)
#define MAX_MANIFEST (64u * 1024u)
#define TEST_BASE (MANIFEST_BASE + MAX_MANIFEST)
#define TEST_BYTES (64u * 1024u)
#define MAX_CHUNK 4096u
#define MAX_WIRE_CHUNK 8192u
#define MAX_WINDOW 16u
#define PACKAGE_PROTOCOL_VERSION 3u
#define PACKAGE_HEADER_BYTES 144u
#define PREFLIGHT_HEADER_BYTES 32u
#define FRAME_HEADER_BYTES 40u
#define PKG_MAGIC0 0x534f4d50u /* PMOS */
#define PKG_MAGIC1 0x33474b50u /* PKG3 */
#define FRAME_MAGIC 0x33464b50u /* PKF3 */
#define ACK_MAGIC 0x334b4341u /* ACK3 */
#define ACK_CONFIRM_BYTE 0xa5u
#define ACK_CONFIRM_TIMEOUT_MS 1500u
#define ACK_CONFIRM_RETRIES 4u
#define BAUD_TEST_MAGIC 0x33544442u /* BDT3 */
#define PREFLIGHT_MAGIC0 0x534f4d50u /* PMOS */
#define PREFLIGHT_MAGIC1 0x31544650u /* PFT1 */
#define PREFLIGHT_PROTOCOL_VERSION 1u
#define PREFLIGHT_FLAG_RESTORE 0x00000001u
#define PREFLIGHT_KNOWN_FLAGS PREFLIGHT_FLAG_RESTORE
#define DEFAULT_PREFLIGHT_SCRATCH 0x00ff0000u
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
#define CONFIRM_TIMEOUT_MS 0xffffffffu
#define BAUD_TEST_TIMEOUT_MS 6000u
#define V3_FRAME_TIMEOUT_MS 5000u
#define FEATURE_DRAIN_IDLE_MS 20u
#define FEATURE_DRAIN_MAX_MS 5000u
#define V3_FRAME_ERROR_NONE 0u
#define V3_FRAME_ERROR_HEADER_TIMEOUT 1u
#define V3_FRAME_ERROR_HEADER_CRC 2u
#define V3_FRAME_ERROR_MAGIC 3u
#define V3_FRAME_ERROR_OBJECT 4u
#define V3_FRAME_ERROR_SEQUENCE 5u
#define V3_FRAME_ERROR_GEOMETRY 6u
#define V3_FRAME_ERROR_PAYLOAD_TIMEOUT 7u
#define V3_FRAME_ERROR_WIRE_CRC 8u
#define V3_FRAME_ERROR_DECODE 9u
#define V3_FRAME_ERROR_DECODED_CRC 10u
#define V3_FRAME_ERROR_SLOT 11u
#define V3_FRAME_ERROR_OBJECT_CRC 12u
#define V3_FRAME_ERROR_OBJECT_SHA 13u
#define UART_BASELINE_BAUD 115200u
#define V3_FRAME_FLAG_LZ4 0x00000001u
#define V3_REPR_RAW 0u
#define V3_REPR_SPARSE 1u
#define V3_REPR_LZ4 2u
#define V3_REPR_SPARSE_LZ4 3u
#define SPI_STATUS_WIP 0x01u
#define SPI_STATUS_WEL 0x02u
#define SPI_STATUS_BP_MASK 0x3cu
#define SPI_SFDP_BYTES 8u

#ifndef RECOVERY_SOC_FAMILY
#error RECOVERY_SOC_FAMILY must be 1 for Luton26 or 2 for Jaguar1
#endif

#if RECOVERY_SOC_FAMILY == 1
#define RECOVERY_SOC_NAME "luton26"
#define SPI_SW_MODE_ADDR 0x70000064u
#define SPI_GENERAL_CTRL_ADDR 0x70000024u
#define SPI_GENERAL_CTRL_ENABLE_MASK 0u
#define RECOVERY_DESCRIPTOR "PMOSRECOVERY3;SOC=luton26;FAMILY=1;SPI=70000064;PROTO=3;PREFLIGHT=4;BAUDTEST=1;FRAME_MAX=4096;WINDOW_MAX=16;ACKFMT=BIN1;SPARSE=1;LZ4=1;CONFIRM_RETRY=1;AUTO_CONFIRM=1;AUTO_REBOOT=1;END"
#elif RECOVERY_SOC_FAMILY == 2
#define RECOVERY_SOC_NAME "jaguar1"
#define SPI_SW_MODE_ADDR 0x70000068u
#define SPI_GENERAL_CTRL_ADDR 0x70000028u
#define SPI_GENERAL_CTRL_ENABLE_MASK (1u << 2)
#define RECOVERY_DESCRIPTOR "PMOSRECOVERY3;SOC=jaguar1;FAMILY=2;SPI=70000068;PROTO=3;PREFLIGHT=4;BAUDTEST=1;FRAME_MAX=4096;WINDOW_MAX=16;ACKFMT=BIN1;SPARSE=1;LZ4=1;CONFIRM_RETRY=1;AUTO_CONFIRM=1;AUTO_REBOOT=1;END"
#else
#error unsupported RECOVERY_SOC_FAMILY
#endif

#define SPI_SW_PIN_CTRL_MODE (1u << 13)
#define SPI_SW_SCK           (1u << 12)
#define SPI_SW_SCK_OE        (1u << 11)
#define SPI_SW_SDO           (1u << 10)
#define SPI_SW_SDO_OE        (1u << 9)
#define SPI_SW_CS_SHIFT      5u
#define SPI_SW_CS_MASK       (0x0fu << SPI_SW_CS_SHIFT)
#define SPI_SW_CS_OE_SHIFT   1u
#define SPI_SW_SDI           (1u << 0)
#define SPI_CS0_MASK         0x01u
#define SPI_CS_NONE          0x00u
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

struct package_v3_header {
    u32 magic0;
    u32 magic1;
    u32 version;
    u32 flags;
    u32 soc_family;
    u32 image_size;
    u32 manifest_size;
    u32 frame_size;
    u32 window_size;
    u32 representation;
    u32 image_frame_count;
    u32 manifest_frame_count;
    u32 image_wire_bytes;
    u32 manifest_crc32;
    u32 image_crc32;
    u8 image_sha256[32];
    u8 manifest_sha256[32];
    char target_model[16];
    u32 header_crc32;
};

struct frame_header {
    u32 magic;
    u32 object_id;
    u32 sequence;
    u32 offset;
    u32 decoded_length;
    u32 wire_length;
    u32 flags;
    u32 wire_crc32;
    u32 decoded_crc32;
    u32 header_crc32;
};

struct ack_record {
    u32 magic;
    u32 object_id;
    u32 window_base;
    u32 window_count;
    u32 retry_bitmap;
    u32 status;
    u32 crc32;
};

struct preflight_header {
    u32 magic0;
    u32 magic1;
    u32 version;
    u32 flags;
    u32 scratch_address;
    u32 scratch_size;
    u32 pattern_seed;
    u32 header_crc32;
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
static volatile u32 * const spi_general_ctrl = (volatile u32 *)SPI_GENERAL_CTRL_ADDR;
static u8 preflight_page[256];
static u32 uart_error_flags;
static u32 uart_pushback_valid;
static u8 uart_pushback_byte;
static u32 v3_last_frame_error;
static u32 v3_last_expected_sequence;

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
    u32 status = uart[UART_LSR / 4];
    uart_error_flags |= status & UART_LSR_ERRORS;
    return (status & UART_LSR_DR) != 0;
}

/* The timeout covers the complete requested block. */
static int recv_exact(u8 *destination, u32 length, u32 milliseconds)
{
    struct pmos_timer timer;
    u32 received = 0u;
    timer_start(&timer);
    if (uart_pushback_valid && received < length) {
        destination[received++] = uart_pushback_byte;
        uart_pushback_valid = 0u;
    }
    while (received < length) {
        while (!rx_ready()) {
            if (timer_expired(&timer, milliseconds)) return 0;
        }
        destination[received++] = (u8)(uart[0] & 0xffu);
        if (timer_expired(&timer, milliseconds) && received < length) return 0;
    }
    return 1;
}

static void uart_put_raw(u8 value)
{
    while ((uart[UART_LSR / 4] & UART_LSR_THRE) == 0u) {}
    uart[0] = (u32)value;
}

static void putc_b(char c)
{
    if (c == '\n') uart_put_raw((u8)'\r');
    uart_put_raw((u8)c);
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
    char hardware_contract[64];
    char adaptive_contract[64];
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
        protocol_version != 2u ||
        !json_string_value(uart_object, uart_length, "loader_sha256", loader_sha, sizeof(loader_sha))) {
        puts_b("PMOSREC RESULT ERROR MANIFEST-RECOVERY-CAPABILITY\n"); return -1;
    }

    if (!json_object_value(recovery_object, recovery_length, "uart_firmware", &firmware_object, &firmware_length) ||
        !json_true_value(firmware_object, firmware_length, "enabled") ||
        !json_u32_value(firmware_object, firmware_length, "protocol_version", &protocol_version) ||
        protocol_version != PACKAGE_PROTOCOL_VERSION ||
        !json_u32_value(firmware_object, firmware_length, "full_image_bytes", &full_image_bytes) ||
        full_image_bytes != FULL_IMAGE_SIZE ||
        !json_string_value(firmware_object, firmware_length, "hardware_preflight_contract",
                           hardware_contract, sizeof(hardware_contract)) ||
        !text_equal(hardware_contract, "spi-nor-scratch-rw-restore-loader-crc-v4") ||
        !json_string_value(firmware_object, firmware_length, "adaptive_transport_contract",
                           adaptive_contract, sizeof(adaptive_contract)) ||
        !text_equal(adaptive_contract, "pmosrec-v3-adaptive-uart-sparse-lz4-v1") ||
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
                                    "transport_integrity", "object-sha256") ||
        !json_array_contains_string(firmware_object, firmware_length,
                                    "transport_integrity", "compact-ack-crc32") ||
        !json_array_contains_string(firmware_object, firmware_length,
                                    "transport_integrity", "reconstructed-image-sha256")) {
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

static int spi_controller_prepare(void)
{
    u32 before = *spi_general_ctrl;
    u32 requested = before | SPI_GENERAL_CTRL_ENABLE_MASK;
    u32 observed;
    if (requested != before) *spi_general_ctrl = requested;
    __asm__ __volatile__("sync" ::: "memory");
    observed = *spi_general_ctrl;
    puts_b("PMOSREC SPI-GENERAL BEFORE="); hex32(before);
    puts_b(" REQUESTED="); hex32(requested); puts_b(" OBSERVED="); hex32(observed); puts_b("\n");
    if ((observed & SPI_GENERAL_CTRL_ENABLE_MASK) != SPI_GENERAL_CTRL_ENABLE_MASK) {
        puts_b("PMOSREC RESULT ERROR SPI-MASTER-ENABLE\n");
        return -1;
    }

    /* Release any stale software-pin state inherited from an earlier stage. */
    *spi_sw = 0u;
    __asm__ __volatile__("sync" ::: "memory");
    puts_b("PMOSREC SPI-CS-CONTRACT ACTIVE-MASK CS0=00000001 NONE=00000000\n");
    return 0;
}

static void spi_delay(void)
{
    u32 start = cp0_count();
    while ((u32)(cp0_count() - start) < SPI_HALF_PERIOD_TICKS) {}
}

static u32 spi_active_base(void)
{
    /*
     * The MSCC SW_MODE chip-select field is an active-mask, not a level map:
     * BIT(n) asserts CSn and zero deselects all chip selects.  This mirrors
     * the working upstream mscc_bb_spi driver exactly.
     */
    return SPI_SW_PIN_CTRL_MODE | SPI_SW_SCK_OE | SPI_SW_SDO_OE |
           (SPI_CS0_MASK << SPI_SW_CS_OE_SHIFT) |
           (SPI_CS0_MASK << SPI_SW_CS_SHIFT);
}

static void spi_write(u32 value)
{
    *spi_sw = value;
    __asm__ __volatile__("sync" ::: "memory");
    spi_delay();
}

static void spi_deselect(void)
{
    u32 value = *spi_sw;

    /* Actively deselect CS0 while preserving the current clock level. */
    value &= ~SPI_SW_CS_MASK;
    spi_write(value);

    /* Stop driving SCK, then release the complete software-mode register. */
    value &= ~SPI_SW_SCK_OE;
    spi_write(value);
    spi_write(0u);
}

static void spi_select(void)
{
    u32 value = spi_active_base();

    /* SPI mode 0: start high, then the first data setup drives SCK low. */
    spi_write(value | SPI_SW_SCK);
}

static u8 spi_xfer(u8 output)
{
    u8 input = 0u;
    u32 base = spi_active_base();
    int bit;

    for (bit = 7; bit >= 0; bit--) {
        u32 value = base;
        if (((output >> bit) & 1u) != 0u) value |= SPI_SW_SDO;

        /* Data setup on SCK low, target samples on rising edge. */
        spi_write(value);
        spi_write(value | SPI_SW_SCK);

        /* Sample close to the following falling edge, as the MSCC driver does. */
        if ((*spi_sw & SPI_SW_SDI) != 0u) input |= (u8)(1u << bit);
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

static void spi_read_block(u32 address, u8 *destination, u32 length)
{
    u32 offset;
    spi_select(); spi_xfer(0x03u);
    spi_xfer((u8)(address >> 16)); spi_xfer((u8)(address >> 8)); spi_xfer((u8)address);
    for (offset = 0u; offset < length; offset++) destination[offset] = spi_xfer(0xffu);
    spi_deselect();
}

static u32 spi_crc_range(u32 address, u32 length)
{
    u32 crc = 0xffffffffu;
    u32 offset;
    u8 value;
    spi_select(); spi_xfer(0x03u);
    spi_xfer((u8)(address >> 16)); spi_xfer((u8)(address >> 8)); spi_xfer((u8)address);
    for (offset = 0u; offset < length; offset++) {
        value = spi_xfer(0xffu);
        crc = pmos_crc32_update(crc, &value, 1u);
    }
    spi_deselect();
    return ~crc;
}

static void spi_read_sfdp(u8 data[SPI_SFDP_BYTES])
{
    u32 i;
    spi_select(); spi_xfer(0x5au);
    spi_xfer(0u); spi_xfer(0u); spi_xfer(0u); spi_xfer(0xffu);
    for (i = 0u; i < SPI_SFDP_BYTES; i++) data[i] = spi_xfer(0xffu);
    spi_deselect();
}

static int spi_erase_block(const struct spi_device *device, u32 address)
{
    if (spi_write_enable() != 0) return -1;
    spi_select(); spi_xfer(device->erase_opcode);
    spi_xfer((u8)(address >> 16)); spi_xfer((u8)(address >> 8)); spi_xfer((u8)address);
    spi_deselect();
    if (spi_wait_ready(device->erase_timeout_ms) != 0) return -1;
    return spi_check_completion(device, address, "PREFLIGHT-ERASE");
}

static int spi_program_page(const struct spi_device *device, u32 address, const u8 *data)
{
    u32 offset;
    if (spi_write_enable() != 0) return -1;
    spi_select(); spi_xfer(0x02u);
    spi_xfer((u8)(address >> 16)); spi_xfer((u8)(address >> 8)); spi_xfer((u8)address);
    for (offset = 0u; offset < device->page_size; offset++) spi_xfer(data[offset]);
    spi_deselect();
    if (spi_wait_ready(device->program_timeout_ms) != 0) return -1;
    return spi_check_completion(device, address, "PREFLIGHT-PROGRAM");
}

static u8 preflight_pattern_byte(u32 offset, u32 seed)
{
    u32 value = seed ^ (offset * 0x45d9f3bu) ^ (offset << 16) ^ (offset >> 3);
    value ^= value << 13; value ^= value >> 17; value ^= value << 5;
    return (u8)(value ^ (value >> 8) ^ (value >> 16) ^ (value >> 24));
}

static int spi_verify_erased(u32 address, u32 length)
{
    u32 offset;
    spi_select(); spi_xfer(0x03u);
    spi_xfer((u8)(address >> 16)); spi_xfer((u8)(address >> 8)); spi_xfer((u8)address);
    for (offset = 0u; offset < length; offset++) {
        if (spi_xfer(0xffu) != 0xffu) { spi_deselect(); return -1; }
    }
    spi_deselect();
    return 0;
}

static int spi_verify_buffer(u32 address, const u8 *expected, u32 length)
{
    u32 offset;
    spi_select(); spi_xfer(0x03u);
    spi_xfer((u8)(address >> 16)); spi_xfer((u8)(address >> 8)); spi_xfer((u8)address);
    for (offset = 0u; offset < length; offset++) {
        if (spi_xfer(0xffu) != expected[offset]) { spi_deselect(); return -1; }
    }
    spi_deselect();
    return 0;
}

static int spi_verify_pattern(u32 address, u32 length, u32 seed)
{
    u32 offset;
    spi_select(); spi_xfer(0x03u);
    spi_xfer((u8)(address >> 16)); spi_xfer((u8)(address >> 8)); spi_xfer((u8)address);
    for (offset = 0u; offset < length; offset++) {
        if (spi_xfer(0xffu) != preflight_pattern_byte(offset, seed)) {
            spi_deselect(); return -1;
        }
    }
    spi_deselect();
    return 0;
}

static int spi_program_buffer(const struct spi_device *device, u32 address,
                              const u8 *data, u32 length)
{
    u32 offset;
    for (offset = 0u; offset < length; offset += device->page_size) {
        if (spi_program_page(device, address + offset, data + offset) != 0) return -1;
    }
    return 0;
}

static int spi_program_pattern(const struct spi_device *device, u32 address,
                               u32 length, u32 seed)
{
    u32 page, offset;
    for (page = 0u; page < length; page += device->page_size) {
        for (offset = 0u; offset < device->page_size; offset++)
            preflight_page[offset] = preflight_pattern_byte(page + offset, seed);
        if (spi_program_page(device, address + page, preflight_page) != 0) return -1;
    }
    return 0;
}

static int spi_scratch_preflight(const struct spi_device *device,
                                 const struct preflight_header *request)
{
    u8 *backup = STAGING_BASE;
    u32 original_crc;
    u32 loader_crc_before;
    u32 loader_crc_after;
    int test_ok = 1;
    int restore_ok = 1;

    if (request->version != PREFLIGHT_PROTOCOL_VERSION ||
        (request->flags & ~PREFLIGHT_KNOWN_FLAGS) != 0u ||
        (request->flags & PREFLIGHT_FLAG_RESTORE) == 0u ||
        request->scratch_size != device->erase_size ||
        request->scratch_address < LOADER_REGION_SIZE ||
        request->scratch_address > device->bytes - device->erase_size ||
        (request->scratch_address & (device->erase_size - 1u)) != 0u) {
        puts_b("PMOSREC RESULT ERROR PREFLIGHT-REQUEST\n"); return -1;
    }

    puts_b("PMOSPFT BEGIN ADDRESS="); hex32(request->scratch_address);
    puts_b(" BYTES="); hex32(request->scratch_size); puts_b(" SEED=");
    hex32(request->pattern_seed); puts_b("\n");
    loader_crc_before = spi_crc_range(0u, LOADER_REGION_SIZE);
    puts_b("PMOSPFT BOOTLOADER-CRC BEFORE="); hex32(loader_crc_before); puts_b("\n");
    spi_read_block(request->scratch_address, backup, request->scratch_size);
    original_crc = pmos_crc32(backup, request->scratch_size);
    puts_b("PMOSPFT BACKUP CRC32="); hex32(original_crc); puts_b("\n");

    if (spi_erase_block(device, request->scratch_address) != 0 ||
        spi_verify_erased(request->scratch_address, request->scratch_size) != 0) {
        puts_b("PMOSPFT FAIL TEST-ERASE\n"); test_ok = 0;
    } else {
        puts_b("PMOSPFT PASS TEST-ERASE\n");
    }
    if (test_ok && spi_program_pattern(device, request->scratch_address,
                                       request->scratch_size, request->pattern_seed) != 0) {
        puts_b("PMOSPFT FAIL TEST-PROGRAM\n"); test_ok = 0;
    }
    if (test_ok && spi_verify_pattern(request->scratch_address, request->scratch_size,
                                      request->pattern_seed) != 0) {
        puts_b("PMOSPFT FAIL TEST-READBACK\n"); test_ok = 0;
    } else if (test_ok) {
        puts_b("PMOSPFT PASS TEST-PROGRAM-READBACK\n");
    }

    puts_b("PMOSPFT RESTORE-BEGIN\n");
    if (spi_erase_block(device, request->scratch_address) != 0 ||
        spi_verify_erased(request->scratch_address, request->scratch_size) != 0 ||
        spi_program_buffer(device, request->scratch_address, backup, request->scratch_size) != 0 ||
        spi_verify_buffer(request->scratch_address, backup, request->scratch_size) != 0) {
        restore_ok = 0;
    }
    if (!restore_ok) {
        puts_b("PMOSREC RESULT ERROR PREFLIGHT-RESTORE-FAILED SCRATCH=");
        hex32(request->scratch_address); puts_b("\n"); return -1;
    }
    puts_b("PMOSPFT PASS RESTORE CRC32="); hex32(original_crc); puts_b("\n");
    loader_crc_after = spi_crc_range(0u, LOADER_REGION_SIZE);
    if (loader_crc_after != loader_crc_before) {
        puts_b("PMOSREC RESULT ERROR PREFLIGHT-BOOTLOADER-CHANGED BEFORE=");
        hex32(loader_crc_before); puts_b(" AFTER="); hex32(loader_crc_after); puts_b("\n");
        return -1;
    }
    puts_b("PMOSPFT PASS BOOTLOADER-UNCHANGED CRC32="); hex32(loader_crc_after); puts_b("\n");
    if (!test_ok) {
        puts_b("PMOSREC RESULT ERROR PREFLIGHT-RW-FAILED RESTORE=OK\n"); return -1;
    }
    puts_b("PMOSREC RESULT PREFLIGHT-OK SCRATCH="); hex32(request->scratch_address);
    puts_b(" BYTES="); hex32(request->scratch_size); puts_b("\n");
    return 0;
}

static int spi_preflight(const struct spi_device **device_out, u8 id[3])
{
    u8 status;
    const struct spi_device *device;
    u8 sfdp[SPI_SFDP_BYTES];
    spi_deselect();
    spi_read_id(id);
    puts_b("PMOSREC FLASH-ID "); hex8(id[0]); hex8(id[1]); hex8(id[2]); puts_b("\n");
    if ((id[0] == 0xffu && id[1] == 0xffu && id[2] == 0xffu) ||
        (id[0] == 0u && id[1] == 0u && id[2] == 0u)) {
        puts_b("PMOSREC RESULT ERROR FLASH-NO-RESPONSE\n"); return -1;
    }
    device = find_spi_device(id);
    if (!device || device->bytes != FULL_IMAGE_SIZE) {
        puts_b("PMOSREC RESULT ERROR UNSUPPORTED-JEDEC\n"); return -1;
    }
    spi_read_sfdp(sfdp);
    puts_b("PMOSREC SFDP ");
    hex8(sfdp[0]); hex8(sfdp[1]); hex8(sfdp[2]); hex8(sfdp[3]);
    if (sfdp[0] == 0x53u && sfdp[1] == 0x46u && sfdp[2] == 0x44u && sfdp[3] == 0x50u)
        puts_b(" STATUS=PASS\n");
    else
        puts_b(" STATUS=UNAVAILABLE\n");
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

static u8 v3_wire_buffer[MAX_WIRE_CHUNK];
static u8 v3_decode_buffer[MAX_CHUNK];
static u8 v3_received_map[FULL_IMAGE_SIZE / 1024u / 8u];

#define UART_LCR 12u
#define UART_LSR_TEMT 0x40u
#define UART_LCR_DLAB 0x80u
#define UART_LCR_WLEN8 0x03u
#define OBJECT_TEST 3u

#if RECOVERY_SOC_FAMILY == 1
#define SOFT_CHIP_RESET_ADDR 0x60070090u
#define WATCHDOG_TIMER_ADDR  0x70000208u
#else
#define SOFT_CHIP_RESET_ADDR 0x60010090u
#define WATCHDOG_TIMER_ADDR  0x70000200u
#endif
#define SOFT_CHIP_RESET_BIT  0x00000001u
#define WATCHDOG_ENABLE_BIT  0x00000100u
#define WATCHDOG_LOCK_FAST   0x00000001u

static void sleep_ms(u32 milliseconds)
{
    struct pmos_timer timer;
    timer_start(&timer);
    while (!timer_expired(&timer, milliseconds)) {}
}

static u32 uart_drain_until_idle(u32 idle_ms, u32 maximum_ms)
{
    struct pmos_timer idle_timer, total_timer;
    u32 dropped = 0u;
    timer_start(&idle_timer);
    timer_start(&total_timer);
    while (!timer_expired(&total_timer, maximum_ms)) {
        if (rx_ready()) {
            (void)uart[0];
            dropped++;
            timer_start(&idle_timer);
            continue;
        }
        if (timer_expired(&idle_timer, idle_ms)) break;
    }
    return dropped;
}

static int v3_recover_single_frame_boundary(u32 allow_retry)
{
    if (!allow_retry) return -1;
    uart_pushback_valid = 0u;
    (void)uart_drain_until_idle(FEATURE_DRAIN_IDLE_MS, FEATURE_DRAIN_MAX_MS);
    return 1;
}

static void uart_wait_tx_empty(void)
{
    while ((uart[UART_LSR / 4] & UART_LSR_TEMT) == 0u) {}
}

static u32 uart_get_divisor(void)
{
    u32 lcr = uart[UART_LCR / 4];
    u32 low, high;
    uart_wait_tx_empty();
    uart[UART_LCR / 4] = lcr | UART_LCR_DLAB;
    low = uart[0] & 0xffu;
    high = uart[1] & 0xffu;
    uart[UART_LCR / 4] = lcr & ~UART_LCR_DLAB;
    return low | (high << 8);
}

static void uart_set_divisor(u32 divisor)
{
    u32 lcr = uart[UART_LCR / 4];
    if (divisor == 0u) divisor = 1u;
    if (divisor > 0xffffu) divisor = 0xffffu;
    uart_wait_tx_empty();
    uart[UART_LCR / 4] = lcr | UART_LCR_DLAB;
    uart[0] = divisor & 0xffu;
    uart[1] = (divisor >> 8) & 0xffu;
    uart[UART_LCR / 4] = UART_LCR_WLEN8;
    sleep_ms(2u);
}

static u32 uart_actual_rate(u32 clock_hz, u32 divisor)
{
    if (divisor == 0u) divisor = 1u;
    return clock_hz / (16u * divisor);
}

static u32 uart_nearest_divisor(u32 clock_hz, u32 requested)
{
    u32 divisor;
    if (requested == 0u) return 0xffffu;
    divisor = (clock_hz + requested * 8u) / (requested * 16u);
    if (divisor == 0u) divisor = 1u;
    if (divisor > 0xffffu) divisor = 0xffffu;
    return divisor;
}

static int recv_line_timeout(char *line, u32 capacity, u32 timeout_ms)
{
    struct pmos_timer timer;
    u32 used = 0u;
    u8 value;
    if (capacity < 2u) return 0;
    timer_start(&timer);
    for (;;) {
        while (!rx_ready()) {
            if (timeout_ms != 0xffffffffu && timer_expired(&timer, timeout_ms)) return 0;
        }
        value = (u8)(uart[0] & 0xffu);
        if (value == '\r' || value == '\n') {
            if (used == 0u) continue;
            line[used] = 0;
            return 1;
        }
        if (used + 1u < capacity) line[used++] = (char)value;
    }
}

static u32 split_tokens(char *line, char **tokens, u32 maximum)
{
    u32 count = 0u;
    char *p = line;
    while (*p && count < maximum) {
        while (*p == ' ') p++;
        if (!*p) break;
        tokens[count++] = p;
        while (*p && *p != ' ') p++;
        if (*p) *p++ = 0;
    }
    return count;
}

static int parse_u32_token(const char *text, u32 *value)
{
    u32 base = 10u, result = 0u, digit;
    if (!text || !*text) return 0;
    if (text[0] == '0' && (text[1] == 'x' || text[1] == 'X')) {
        base = 16u;
        text += 2;
    }
    if (!*text) return 0;
    while (*text) {
        if (*text >= '0' && *text <= '9') digit = (u32)(*text - '0');
        else if (*text >= 'a' && *text <= 'f') digit = 10u + (u32)(*text - 'a');
        else if (*text >= 'A' && *text <= 'F') digit = 10u + (u32)(*text - 'A');
        else return 0;
        if (digit >= base) return 0;
        result = result * base + digit;
        text++;
    }
    *value = result;
    return 1;
}

static void put_u32_dec(u32 value)
{
    char buffer[11];
    u32 used = 0u;
    if (value == 0u) { putc_b('0'); return; }
    while (value && used < sizeof(buffer)) {
        buffer[used++] = (char)('0' + (value % 10u));
        value /= 10u;
    }
    while (used) putc_b(buffer[--used]);
}

static void put_u32_le(u8 *out, u32 value)
{
    out[0] = (u8)value;
    out[1] = (u8)(value >> 8);
    out[2] = (u8)(value >> 16);
    out[3] = (u8)(value >> 24);
}

static int send_ack_record(u32 object_id, u32 base, u32 count,
                           u32 retry_bitmap, u32 status)
{
    u8 raw[28];
    u8 confirmation;
    u32 i, attempt;
    put_u32_le(raw + 0u, ACK_MAGIC);
    put_u32_le(raw + 4u, object_id);
    put_u32_le(raw + 8u, base);
    put_u32_le(raw + 12u, count);
    put_u32_le(raw + 16u, retry_bitmap);
    put_u32_le(raw + 20u, status);
    put_u32_le(raw + 24u, pmos_crc32(raw, 24u));
    for (attempt = 0u; attempt < ACK_CONFIRM_RETRIES; attempt++) {
        for (i = 0u; i < sizeof(raw); i++) uart_put_raw(raw[i]);
        if (recv_exact(&confirmation, 1u, ACK_CONFIRM_TIMEOUT_MS)) {
            if (confirmation == ACK_CONFIRM_BYTE) return 0;
            /* If the one-byte confirmation was lost but the host already
             * started the next frame, accepting the frame-magic prefix is
             * safe. Preserve it so the next recv_exact() sees an intact
             * frame header instead of desynchronizing the stream. */
            if (confirmation == (u8)(FRAME_MAGIC & 0xffu)) {
                uart_pushback_byte = confirmation;
                uart_pushback_valid = 1u;
                return 0;
            }
        }
    }
    puts_b("PMOSREC RESULT ERROR ACK-CONFIRM-TIMEOUT\n");
    return -1;
}

static u32 xorshift32(u32 *state)
{
    u32 x = *state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    *state = x;
    return x;
}

static u8 prng_byte(u32 *state)
{
    return (u8)xorshift32(state);
}

static u32 deterministic_crc(u32 seed, u32 length)
{
    u32 crc = 0xffffffffu;
    u8 byte;
    while (length--) {
        byte = prng_byte(&seed);
        crc = pmos_crc32_update(crc, &byte, 1u);
    }
    return ~crc;
}

static int lz4_decompress_block(const u8 *source, u32 source_length,
                                u8 *destination, u32 destination_capacity,
                                u32 *decoded_length)
{
    u32 si = 0u, di = 0u, literal_length, match_length, offset, i;
    u8 token, extension;
    while (si < source_length) {
        token = source[si++];
        literal_length = (u32)(token >> 4);
        if (literal_length == 15u) {
            do {
                if (si >= source_length) return 0;
                extension = source[si++];
                literal_length += extension;
            } while (extension == 255u);
        }
        if (si + literal_length > source_length || di + literal_length > destination_capacity) return 0;
        for (i = 0u; i < literal_length; i++) destination[di++] = source[si++];
        if (si == source_length) break;
        if (si + 2u > source_length) return 0;
        offset = (u32)source[si] | ((u32)source[si + 1u] << 8);
        si += 2u;
        if (offset == 0u || offset > di) return 0;
        match_length = (u32)(token & 0x0fu);
        if (match_length == 15u) {
            do {
                if (si >= source_length) return 0;
                extension = source[si++];
                match_length += extension;
            } while (extension == 255u);
        }
        match_length += 4u;
        if (di + match_length > destination_capacity) return 0;
        for (i = 0u; i < match_length; i++) {
            destination[di] = destination[di - offset];
            di++;
        }
    }
    *decoded_length = di;
    return 1;
}

static void clear_received_map(void)
{
    u32 i;
    for (i = 0u; i < sizeof(v3_received_map); i++) v3_received_map[i] = 0u;
}

static int frame_slot_mark(u32 offset, u32 frame_size, u32 retry)
{
    u32 slot, byte, bit;
    if (frame_size < 1024u || (offset % frame_size) != 0u) return 0;
    slot = offset / frame_size;
    if (slot >= FULL_IMAGE_SIZE / 1024u) return 0;
    byte = slot >> 3;
    bit = 1u << (slot & 7u);
    if (!retry && (v3_received_map[byte] & bit) != 0u) return 0;
    v3_received_map[byte] |= (u8)bit;
    return 1;
}

static void parse_v3_frame_header(struct frame_header *header, const u8 raw[FRAME_HEADER_BYTES])
{
    header->magic = le32(raw + 0u);
    header->object_id = le32(raw + 4u);
    header->sequence = le32(raw + 8u);
    header->offset = le32(raw + 12u);
    header->decoded_length = le32(raw + 16u);
    header->wire_length = le32(raw + 20u);
    header->flags = le32(raw + 24u);
    header->wire_crc32 = le32(raw + 28u);
    header->decoded_crc32 = le32(raw + 32u);
    header->header_crc32 = le32(raw + 36u);
}

static int receive_v3_frame(u8 *destination, u32 destination_size, u32 object_id,
                            u32 expected_sequence, u32 frame_size, u32 retry,
                            u32 allow_header_retry)
{
    u8 raw[FRAME_HEADER_BYTES];
    struct frame_header frame;
    u32 decoded = 0u, i;
    v3_last_expected_sequence = expected_sequence;
    v3_last_frame_error = V3_FRAME_ERROR_NONE;
    if (!recv_exact(raw, sizeof(raw), V3_FRAME_TIMEOUT_MS)) {
        v3_last_frame_error = V3_FRAME_ERROR_HEADER_TIMEOUT;
        return v3_recover_single_frame_boundary(allow_header_retry);
    }
    parse_v3_frame_header(&frame, raw);
    if (frame.header_crc32 != pmos_crc32(raw, FRAME_HEADER_BYTES - 4u)) {
        v3_last_frame_error = V3_FRAME_ERROR_HEADER_CRC;
        return v3_recover_single_frame_boundary(allow_header_retry);
    }
    if (frame.magic != FRAME_MAGIC) {
        v3_last_frame_error = V3_FRAME_ERROR_MAGIC;
        return v3_recover_single_frame_boundary(allow_header_retry);
    }
    if (frame.object_id != object_id) {
        v3_last_frame_error = V3_FRAME_ERROR_OBJECT;
        return v3_recover_single_frame_boundary(allow_header_retry);
    }
    if (frame.sequence != expected_sequence) {
        v3_last_frame_error = V3_FRAME_ERROR_SEQUENCE;
        return v3_recover_single_frame_boundary(allow_header_retry);
    }
    if (frame.wire_length == 0u || frame.wire_length > MAX_WIRE_CHUNK ||
        frame.decoded_length == 0u || frame.decoded_length > frame_size ||
        frame.offset > destination_size ||
        frame.decoded_length > destination_size - frame.offset ||
        (frame.flags & ~V3_FRAME_FLAG_LZ4) != 0u) {
        v3_last_frame_error = V3_FRAME_ERROR_GEOMETRY;
        return v3_recover_single_frame_boundary(allow_header_retry);
    }
    if (!recv_exact(v3_wire_buffer, frame.wire_length, V3_FRAME_TIMEOUT_MS)) {
        v3_last_frame_error = V3_FRAME_ERROR_PAYLOAD_TIMEOUT;
        return v3_recover_single_frame_boundary(allow_header_retry);
    }
    if (pmos_crc32(v3_wire_buffer, frame.wire_length) != frame.wire_crc32) {
        v3_last_frame_error = V3_FRAME_ERROR_WIRE_CRC;
        return 1;
    }
    if ((frame.flags & V3_FRAME_FLAG_LZ4) != 0u) {
        if (!lz4_decompress_block(v3_wire_buffer, frame.wire_length,
                                  v3_decode_buffer, frame_size, &decoded) ||
            decoded != frame.decoded_length) {
            v3_last_frame_error = V3_FRAME_ERROR_DECODE;
            return 1;
        }
    } else {
        if (frame.wire_length != frame.decoded_length) {
            v3_last_frame_error = V3_FRAME_ERROR_DECODE;
            return 1;
        }
        for (i = 0u; i < frame.decoded_length; i++) v3_decode_buffer[i] = v3_wire_buffer[i];
        decoded = frame.decoded_length;
    }
    if (pmos_crc32(v3_decode_buffer, decoded) != frame.decoded_crc32) {
        v3_last_frame_error = V3_FRAME_ERROR_DECODED_CRC;
        return 1;
    }
    if (!frame_slot_mark(frame.offset, frame_size, retry)) {
        v3_last_frame_error = V3_FRAME_ERROR_SLOT;
        return -1;
    }
    for (i = 0u; i < decoded; i++) destination[frame.offset + i] = v3_decode_buffer[i];
    v3_last_frame_error = V3_FRAME_ERROR_NONE;
    return 0;
}


static int receive_v3_object(u8 *destination, u32 destination_size, u32 object_id,
                             u32 frame_count, u32 frame_size, u32 window_size,
                             u32 fill_ff, u32 expected_crc, const u8 *expected_sha,
                             u32 verify_sha)
{
    u32 i, base, count, retry_bitmap, bit, result, retry_round;
    u32 crc, ack_status;
    struct pmos_timer transfer_timer;
    u8 digest[32];
    pmos_sha256_ctx sha;
    if (frame_count == 0u || frame_size < 1024u || frame_size > MAX_CHUNK ||
        window_size == 0u || window_size > MAX_WINDOW) return -1;
    v3_last_frame_error = V3_FRAME_ERROR_NONE;
    v3_last_expected_sequence = 0u;
    uart_error_flags = 0u;
    for (i = 0u; i < destination_size; i++) destination[i] = fill_ff ? 0xffu : 0u;
    clear_received_map();
    timer_start(&transfer_timer);
    base = 0u;
    while (base < frame_count) {
        if (timer_expired(&transfer_timer, OBJECT_TRANSFER_TIMEOUT_MS)) {
            puts_b("PMOSREC RESULT ERROR OBJECT-TRANSFER-TIMEOUT\n");
            return -1;
        }
        count = frame_count - base;
        if (count > window_size) count = window_size;
        retry_bitmap = 0u;
        for (i = 0u; i < count; i++) {
            result = (u32)receive_v3_frame(destination, destination_size, object_id,
                                           base + i, frame_size, 0u, window_size == 1u);
            if (result == (u32)-1) return -1;
            if (result != 0u) retry_bitmap |= 1u << i;
        }
        ack_status = uart_error_flags;
        uart_error_flags = 0u;
        if (send_ack_record(object_id, base, count, retry_bitmap, ack_status) != 0) return -1;
        retry_round = 0u;
        while (retry_bitmap != 0u) {
            if (++retry_round > 8u) return -1;
            for (bit = 0u; bit < count; bit++) {
                if ((retry_bitmap & (1u << bit)) == 0u) continue;
                result = (u32)receive_v3_frame(destination, destination_size, object_id,
                                               base + bit, frame_size, 1u, window_size == 1u);
                if (result == (u32)-1) return -1;
                if (result == 0u) retry_bitmap &= ~(1u << bit);
            }
            ack_status = uart_error_flags | (retry_round << 16);
            uart_error_flags = 0u;
            if (send_ack_record(object_id, base, count, retry_bitmap, ack_status) != 0) return -1;
        }
        base += count;
    }
    crc = pmos_crc32(destination, destination_size);
    if (crc != expected_crc) {
        v3_last_frame_error = V3_FRAME_ERROR_OBJECT_CRC;
        return -1;
    }
    if (verify_sha) {
        pmos_sha256_init(&sha);
        pmos_sha256_update(&sha, destination, destination_size);
        pmos_sha256_final(&sha, digest);
        if (!pmos_digest_equal(digest, expected_sha, 32u)) {
            v3_last_frame_error = V3_FRAME_ERROR_OBJECT_SHA;
            return -1;
        }
    }
    return 0;
}

static int handle_baud_offer(char **tokens, u32 count, u32 clock_hz, u32 current_rate)
{
    u32 requested, nonce, divisor, actual, error_ppm, difference;
    if (count != 4u || !parse_u32_token(tokens[2], &requested) ||
        !parse_u32_token(tokens[3], &nonce) || requested == 0u) return -1;
    divisor = uart_nearest_divisor(clock_hz, requested);
    actual = uart_actual_rate(clock_hz, divisor);
    difference = actual > requested ? actual - requested : requested - actual;
    /* Preserve the ratio while scaling into a range where multiplying by one
     * million cannot overflow a 32-bit freestanding build. */
    {
        u32 scaled_difference = difference;
        u32 scaled_requested = requested;
        while (scaled_difference > 0xffffffffu / 1000000u && scaled_requested > 1u) {
            scaled_difference = scaled_difference / 2u + scaled_difference % 2u;
            scaled_requested = scaled_requested / 2u + scaled_requested % 2u;
        }
        error_ppm = (scaled_difference * 1000000u) / scaled_requested;
    }
    puts_b("PMOS3 BAUD-CANDIDATE REQUESTED="); put_u32_dec(requested);
    puts_b(" ACTUAL="); put_u32_dec(actual); puts_b(" DIV="); put_u32_dec(divisor);
    puts_b(" ERROR_PPM="); put_u32_dec(error_ppm); puts_b(" CURRENT="); put_u32_dec(current_rate);
    puts_b(" NONCE="); hex32(nonce); puts_b("\n");
    return 0;
}

static int run_baud_test(char **tokens, u32 count, u32 clock_hz, u32 *current_divisor,
                         u32 *current_rate)
{
    char line[160];
    char *parts[10];
    u32 actual, divisor, nonce, bytes, passes, old_divisor, old_rate;
    u32 pass, seed, length, expected_crc, observed_crc, i, part_count;
    u32 sync_try, parsed_nonce;
    u32 t2h_seed, t2h_crc;
    if (count != 7u || !parse_u32_token(tokens[2], &actual) ||
        !parse_u32_token(tokens[3], &divisor) || !parse_u32_token(tokens[4], &nonce) ||
        !parse_u32_token(tokens[5], &bytes) || !parse_u32_token(tokens[6], &passes) ||
        bytes == 0u || bytes > TEST_BYTES || passes == 0u || passes > 4u ||
        uart_actual_rate(clock_hz, divisor) != actual) return -1;
    old_divisor = *current_divisor;
    old_rate = *current_rate;
    puts_b("PMOS3 BAUD-PREPARED RATE="); put_u32_dec(actual); puts_b(" DIV="); put_u32_dec(divisor);
    puts_b(" NONCE="); hex32(nonce); puts_b(" REVERT="); put_u32_dec(old_rate); puts_b(" TEST_MS=6000\n");
    uart_wait_tx_empty();
    sleep_ms(100u);
    uart_set_divisor(divisor);
    /* Do not start binary traffic until the host acknowledges a complete sync line. */
    for (sync_try = 0u; sync_try < 20u; sync_try++) {
        puts_b("PMOS3 BAUD-SYNC NONCE="); hex32(nonce); puts_b(" RATE="); put_u32_dec(actual); puts_b("\n");
        if (recv_line_timeout(line, sizeof(line), 100u)) {
            part_count = split_tokens(line, parts, 10u);
            if (part_count == 3u && text_equal(parts[0], "PMOS3") &&
                text_equal(parts[1], "BAUD-SYNC-ACK") &&
                parse_u32_token(parts[2], &parsed_nonce) && parsed_nonce == nonce) {
                puts_b("PMOS3 BAUD-START NONCE="); hex32(nonce); puts_b("\n");
                break;
            }
        }
    }
    if (sync_try >= 20u) goto fallback;
    for (pass = 0u; pass < passes; pass++) {
        uart_error_flags = 0u;
        if (!recv_line_timeout(line, sizeof(line), BAUD_TEST_TIMEOUT_MS)) goto fallback;
        part_count = split_tokens(line, parts, 10u);
        if (part_count != 7u || !text_equal(parts[0], "PMOS3") ||
            !text_equal(parts[1], "BAUD-H2T") || !parse_u32_token(parts[2], &i) || i != pass ||
            !parse_u32_token(parts[3], &seed) || !parse_u32_token(parts[4], &length) ||
            !parse_u32_token(parts[5], &expected_crc) || !parse_u32_token(parts[6], &i) || i != nonce ||
            length != bytes) goto fallback;
        if (!recv_exact(TEST_BASE, length, BAUD_TEST_TIMEOUT_MS)) goto fallback;
        observed_crc = pmos_crc32(TEST_BASE, length);
        if (observed_crc != expected_crc || observed_crc != deterministic_crc(seed, length) ||
            uart_error_flags != 0u) goto fallback;
        puts_b("PMOS3 BAUD-H2T-OK PASS="); put_u32_dec(pass); puts_b(" CRC="); hex32(observed_crc); puts_b("\n");
        t2h_seed = seed ^ 0xa5a55a5au;
        t2h_crc = deterministic_crc(t2h_seed, length);
        puts_b("PMOS3 BAUD-T2H PASS="); put_u32_dec(pass); puts_b(" SEED="); hex32(t2h_seed);
        puts_b(" BYTES="); put_u32_dec(length); puts_b(" CRC="); hex32(t2h_crc); puts_b(" NONCE="); hex32(nonce); puts_b("\n");
        for (i = 0u; i < length; i++) uart_put_raw(prng_byte(&t2h_seed));
        if (!recv_line_timeout(line, sizeof(line), BAUD_TEST_TIMEOUT_MS)) goto fallback;
        part_count = split_tokens(line, parts, 10u);
        if (part_count != 5u || !text_equal(parts[0], "PMOS3") ||
            !text_equal(parts[1], "BAUD-T2H-ACK") || !parse_u32_token(parts[2], &i) || i != pass ||
            !parse_u32_token(parts[3], &observed_crc) || observed_crc != t2h_crc ||
            !parse_u32_token(parts[4], &i) || i != nonce) goto fallback;
    }
    puts_b("PMOS3 BAUD-PASS RATE="); put_u32_dec(actual); puts_b(" NONCE="); hex32(nonce); puts_b("\n");
    if (!recv_line_timeout(line, sizeof(line), BAUD_TEST_TIMEOUT_MS)) goto fallback;
    part_count = split_tokens(line, parts, 10u);
    if (part_count != 3u || !text_equal(parts[0], "PMOS3") ||
        !text_equal(parts[1], "BAUD-COMMIT") || !parse_u32_token(parts[2], &i) || i != nonce)
        goto fallback;
    *current_divisor = divisor;
    *current_rate = actual;
    puts_b("PMOS3 BAUD-COMMITTED RATE="); put_u32_dec(actual); puts_b(" NONCE="); hex32(nonce); puts_b("\n");
    return 0;
fallback:
    uart_set_divisor(old_divisor);
    *current_divisor = old_divisor;
    *current_rate = old_rate;
    /* The host independently returns to old_rate. Keep a generous discovery
     * window, then mark the exact point at which command parsing resumes. */
    for (i = 0u; i < 30u; i++) {
        puts_b("PMOS3 BAUD-FALLBACK RATE="); put_u32_dec(old_rate); puts_b(" NONCE="); hex32(nonce); puts_b("\n");
        sleep_ms(100u);
    }
    puts_b("PMOS3 BAUD-FALLBACK-READY RATE="); put_u32_dec(old_rate);
    puts_b(" NONCE="); hex32(nonce); puts_b("\n");
    return 1;
}

static int handle_feature_test(char **tokens, u32 count)
{
    u32 mode, frame_size, window_size, frame_count, size, crc, dropped;
    if (count != 8u || !parse_u32_token(tokens[2], &mode) ||
        !parse_u32_token(tokens[3], &frame_size) || !parse_u32_token(tokens[4], &window_size) ||
        !parse_u32_token(tokens[5], &frame_count) || !parse_u32_token(tokens[6], &size) ||
        !parse_u32_token(tokens[7], &crc) || size == 0u || size > TEST_BYTES ||
        mode > V3_REPR_SPARSE_LZ4) return -1;
    puts_b("PMOS3 FEATURE-READY MODE="); put_u32_dec(mode); puts_b(" FRAME="); put_u32_dec(frame_size);
    puts_b(" WINDOW="); put_u32_dec(window_size); puts_b("\n");
    if (receive_v3_object(TEST_BASE, size, OBJECT_TEST, frame_count, frame_size,
                          window_size, 1u, crc, (const u8 *)0, 0u) != 0) {
        uart_pushback_valid = 0u;
        dropped = uart_drain_until_idle(FEATURE_DRAIN_IDLE_MS, FEATURE_DRAIN_MAX_MS);
        puts_b("PMOS3 FEATURE-FAIL MODE="); put_u32_dec(mode);
        puts_b(" FRAME-ERROR="); put_u32_dec(v3_last_frame_error);
        puts_b(" EXPECTED-SEQ="); put_u32_dec(v3_last_expected_sequence);
        puts_b(" UARTERR="); hex32(uart_error_flags);
        puts_b(" DRAINED="); put_u32_dec(dropped); puts_b("\n");
        uart_error_flags = 0u;
        return 1;
    }
    puts_b("PMOS3 FEATURE-PASS MODE="); put_u32_dec(mode); puts_b("\n");
    return 0;
}

static void parse_v3_package_header(struct package_v3_header *header, const u8 *raw)
{
    u32 i;
    header->magic0 = le32(raw + 0u); header->magic1 = le32(raw + 4u);
    header->version = le32(raw + 8u); header->flags = le32(raw + 12u);
    header->soc_family = le32(raw + 16u); header->image_size = le32(raw + 20u);
    header->manifest_size = le32(raw + 24u); header->frame_size = le32(raw + 28u);
    header->window_size = le32(raw + 32u); header->representation = le32(raw + 36u);
    header->image_frame_count = le32(raw + 40u); header->manifest_frame_count = le32(raw + 44u);
    header->image_wire_bytes = le32(raw + 48u); header->manifest_crc32 = le32(raw + 52u);
    header->image_crc32 = le32(raw + 56u);
    for (i = 0u; i < 32u; i++) header->image_sha256[i] = raw[60u + i];
    for (i = 0u; i < 32u; i++) header->manifest_sha256[i] = raw[92u + i];
    for (i = 0u; i < 16u; i++) header->target_model[i] = (char)raw[124u + i];
    header->target_model[15] = 0;
    header->header_crc32 = le32(raw + 140u);
}

static int validate_v3_package_header(const struct package_v3_header *header, const u8 *raw)
{
    if (header->magic0 != PKG_MAGIC0 || header->magic1 != PKG_MAGIC1 ||
        header->version != PACKAGE_PROTOCOL_VERSION ||
        (header->flags & ~KNOWN_FLAGS) != 0u || (header->flags & FLAG_FULL_FLASH) == 0u ||
        header->soc_family != RECOVERY_SOC_FAMILY || header->image_size != FULL_IMAGE_SIZE ||
        header->manifest_size == 0u || header->manifest_size > MAX_MANIFEST ||
        (header->frame_size != 1024u && header->frame_size != 4096u) ||
        header->window_size == 0u || header->window_size > MAX_WINDOW ||
        header->representation > V3_REPR_SPARSE_LZ4 || header->image_frame_count == 0u ||
        header->image_frame_count > (header->image_size + header->frame_size - 1u) / header->frame_size ||
        header->manifest_frame_count != (header->manifest_size + header->frame_size - 1u) / header->frame_size ||
        header->image_wire_bytes == 0u ||
        header->image_wire_bytes > header->image_frame_count * (FRAME_HEADER_BYTES + MAX_WIRE_CHUNK) ||
        header->target_model[0] == 0 || !model_allowed_for_payload(header->target_model) ||
        header->header_crc32 != pmos_crc32(raw, PACKAGE_HEADER_BYTES - 4u)) return 0;
    return 1;
}

static void make_legacy_header(struct package_header *legacy,
                               const struct package_v3_header *header)
{
    u32 i;
    legacy->magic0 = PKG_MAGIC0; legacy->magic1 = PKG_MAGIC1;
    legacy->version = header->version; legacy->flags = header->flags;
    legacy->soc_family = header->soc_family; legacy->image_size = header->image_size;
    legacy->manifest_size = header->manifest_size; legacy->chunk_size = header->frame_size;
    legacy->image_crc32 = header->image_crc32; legacy->manifest_crc32 = header->manifest_crc32;
    for (i = 0u; i < 32u; i++) legacy->image_sha256[i] = header->image_sha256[i];
    for (i = 0u; i < 32u; i++) legacy->manifest_sha256[i] = header->manifest_sha256[i];
    for (i = 0u; i < 16u; i++) legacy->target_model[i] = header->target_model[i];
    legacy->header_crc32 = header->header_crc32;
}

static int validate_manifest_metadata_v3(const struct package_header *header,
                                         const struct spi_device *device, const u8 id[3])
{
    char family[16], status[40], artifact_sha[65];
    char hardware_contract[64], adaptive_contract[64];
    u8 digest_hex[64];
    const u8 *models, *artifact, *recovery, *firmware;
    u32 models_len, artifact_len, recovery_len, firmware_len, bytes, protocol;
    if (!json_string_value(MANIFEST_BASE, header->manifest_size, "target_family", family, sizeof(family)) ||
        !text_equal(family, "vcore3")) return -1;
    if (!json_object_value(MANIFEST_BASE, header->manifest_size, "models", &models, &models_len) ||
        !json_string_value(models, models_len, header->target_model, status, sizeof(status))) return -1;
    if (text_equal(status, "known-incompatible") ||
        (text_equal(status, "untested") && (header->flags & FLAG_FORCE_UNTESTED) == 0u)) return -1;
    if (!json_object_value(MANIFEST_BASE, header->manifest_size, "artifact", &artifact, &artifact_len) ||
        !json_u32_value(artifact, artifact_len, "bytes", &bytes) || bytes != FULL_IMAGE_SIZE ||
        !json_string_value(artifact, artifact_len, "sha256", artifact_sha, sizeof(artifact_sha))) return -1;
    digest_to_hex(header->image_sha256, digest_hex);
    if (!bytes_equal((const u8 *)artifact_sha, digest_hex, 64u)) return -1;
    if (!json_object_value(MANIFEST_BASE, header->manifest_size, "recovery", &recovery, &recovery_len) ||
        !json_object_value(recovery, recovery_len, "uart_firmware", &firmware, &firmware_len) ||
        !json_u32_value(firmware, firmware_len, "protocol_version", &protocol) ||
        protocol != PACKAGE_PROTOCOL_VERSION ||
        !json_u32_value(firmware, firmware_len, "full_image_bytes", &bytes) ||
        bytes != FULL_IMAGE_SIZE ||
        !json_string_value(firmware, firmware_len, "hardware_preflight_contract",
                           hardware_contract, sizeof(hardware_contract)) ||
        !text_equal(hardware_contract, "spi-nor-scratch-rw-restore-loader-crc-v4") ||
        !json_string_value(firmware, firmware_len, "adaptive_transport_contract",
                           adaptive_contract, sizeof(adaptive_contract)) ||
        !text_equal(adaptive_contract, "pmosrec-v3-adaptive-uart-sparse-lz4-v1") ||
        !json_array_contains_string(firmware, firmware_len, "transport_integrity", "frame-crc32") ||
        !json_array_contains_string(firmware, firmware_len, "transport_integrity", "compact-ack-crc32") ||
        !json_array_contains_string(firmware, firmware_len, "transport_integrity", "object-crc32") ||
        !json_array_contains_string(firmware, firmware_len, "transport_integrity", "object-sha256") ||
        !json_array_contains_string(firmware, firmware_len, "transport_integrity",
                                    "reconstructed-image-sha256")) return -1;
    if (validate_flash_manifest(header, device, id) != 0) return -1;
    return 0;
}

static int read_confirmation_once(u32 nonce)
{
    char line[64];
    char expected[32];
    static const char prefix[] = "ERASEFLASH ";
    u32 i, used;
    for (i = 0u; prefix[i]; i++) expected[i] = prefix[i];
    for (used = 0u; used < 8u; used++) {
        u32 shift = 28u - used * 4u;
        u32 nibble = (nonce >> shift) & 0xfu;
        expected[i + used] = (char)(nibble < 10u ? '0' + nibble : 'a' + nibble - 10u);
    }
    expected[i + 8u] = 0;
    if (!recv_line_timeout(line, sizeof(line), CONFIRM_TIMEOUT_MS)) return -1;
    return text_equal(line, expected) ? 0 : -1;
}

static void target_reboot(void)
{
    volatile u32 *reset = (volatile u32 *)SOFT_CHIP_RESET_ADDR;
    volatile u32 *watchdog = (volatile u32 *)WATCHDOG_TIMER_ADDR;
    u32 count;
    for (count = 5u; count > 0u; count--) {
        puts_b("PMOSREC REBOOT "); put_u32_dec(count); puts_b("\n");
        sleep_ms(1000u);
    }
    puts_b("PMOSREC REBOOT NOW\n");
    uart_wait_tx_empty();
    *reset = SOFT_CHIP_RESET_BIT;
    __asm__ __volatile__("sync" ::: "memory");
    /* The GCB soft-chip-reset register is the same mechanism used by platform
     * init. If execution continues, arm the family-specific ICPU watchdog
     * defined by the generated VTSS register headers as a last-resort reset. */
    sleep_ms(250u);
    puts_b("PMOSREC REBOOT FALLBACK-WATCHDOG\n");
    uart_wait_tx_empty();
    *watchdog = WATCHDOG_ENABLE_BIT | WATCHDOG_LOCK_FAST;
    __asm__ __volatile__("sync" ::: "memory");
    for (;;) __asm__ __volatile__("wait");
}

static void halt(void)
{
    for (;;) __asm__ __volatile__("wait");
}

static void run_package_v3(const struct spi_device *device, const u8 id[3])
{
    u8 raw[PACKAGE_HEADER_BYTES];
    struct package_v3_header header;
    struct package_header legacy;
    u32 nonce;
    if (!recv_exact(raw, sizeof(raw), PACKAGE_HEADER_TIMEOUT_MS)) {
        puts_b("PMOSREC RESULT ERROR PACKAGE-HEADER-TIMEOUT\n"); return;
    }
    parse_v3_package_header(&header, raw);
    if (!validate_v3_package_header(&header, raw)) {
        puts_b("PMOSREC RESULT ERROR INVALID-PACKAGE-HEADER\n"); return;
    }
    make_legacy_header(&legacy, &header);
    puts_b("PMOS3 PACKAGE-HEADER-ACK MODEL="); puts_b(header.target_model);
    puts_b(" FRAME="); put_u32_dec(header.frame_size); puts_b(" WINDOW="); put_u32_dec(header.window_size);
    puts_b(" REPRESENTATION="); put_u32_dec(header.representation); puts_b("\n");
    if (receive_v3_object(MANIFEST_BASE, header.manifest_size, OBJECT_MANIFEST,
                          header.manifest_frame_count, header.frame_size, header.window_size,
                          0u, header.manifest_crc32, header.manifest_sha256, 1u) != 0) {
        puts_b("PMOSREC FRAME-ERROR OBJECT=MANIFEST CODE="); put_u32_dec(v3_last_frame_error);
        puts_b(" EXPECTED-SEQ="); put_u32_dec(v3_last_expected_sequence);
        puts_b(" UARTERR="); hex32(uart_error_flags); puts_b("\n");
        puts_b("PMOSREC RESULT ERROR MANIFEST-TRANSFER\n"); return;
    }
    puts_b("PMOS3 MANIFEST-OBJECT-VERIFIED\n");
    if (validate_manifest_metadata_v3(&legacy, device, id) != 0) {
        puts_b("PMOSREC RESULT ERROR MANIFEST-FIRST-VALIDATION\n"); return;
    }
    puts_b("PMOS3 MANIFEST-ACCEPTED\n");
    if (receive_v3_object(STAGING_BASE, header.image_size, OBJECT_IMAGE,
                          header.image_frame_count, header.frame_size, header.window_size,
                          1u, header.image_crc32, header.image_sha256, 1u) != 0) {
        puts_b("PMOSREC FRAME-ERROR OBJECT=IMAGE CODE="); put_u32_dec(v3_last_frame_error);
        puts_b(" EXPECTED-SEQ="); put_u32_dec(v3_last_expected_sequence);
        puts_b(" UARTERR="); hex32(uart_error_flags); puts_b("\n");
        puts_b("PMOSREC RESULT ERROR IMAGE-TRANSFER\n"); return;
    }
    puts_b("PMOS3 IMAGE-OBJECT-VERIFIED\n");
    if (validate_manifest_and_image(&legacy) != 0 || validate_flash_manifest(&legacy, device, id) != 0) return;
    puts_b("PMOSPKG VERIFIED MODEL="); puts_b(header.target_model); puts_b("\n");
    if ((header.flags & FLAG_DRY_RUN) != 0u) {
        puts_b("PMOSREC RESULT DRY-RUN-OK\n"); return;
    }
    nonce = header.image_crc32 ^ header.manifest_crc32 ^ cp0_count() ^
            ((u32)id[0] << 16) ^ ((u32)id[1] << 8) ^ (u32)id[2];
    puts_b("PMOSREC ERASE-CHALLENGE "); hex32(nonce); puts_b("\n");
    puts_b("PMOSREC CONFIRMATION-WAIT ENTER=ERASEFLASH+"); hex32(nonce);
    puts_b(" POWER-CYCLE-TO-CANCEL\n");
    while (read_confirmation_once(nonce) != 0) {
        puts_b("PMOSREC CONFIRMATION-REJECTED EXPECTED=ERASEFLASH "); hex32(nonce);
        puts_b(" RETRY=FOREVER POWER-CYCLE-TO-CANCEL\n");
    }
    puts_b("PMOSREC CONFIRMATION-ACK\n");
    if (spi_erase_full(device) != 0 || spi_program_full(device) != 0 ||
        spi_verify_full(device) != 0) return;
    puts_b("PMOSREC RESULT SUCCESS\n");
    target_reboot();
}

void recovery_main(void)
{
    char line[192];
    char *tokens[12];
    u8 id[3];
    struct preflight_header preflight;
    const struct spi_device *device = (const struct spi_device *)0;
    u32 count, current_divisor, uart_clock, current_rate;
    u32 address, seed;

    current_divisor = uart_get_divisor();
    if (current_divisor == 0u) current_divisor = 56u;
    uart_clock = current_divisor * 16u * UART_BASELINE_BAUD;
    current_rate = uart_actual_rate(uart_clock, current_divisor);

    puts_b("PMOSREC READY 3 SOC=" RECOVERY_SOC_NAME " FAMILY=");
    hex32(RECOVERY_SOC_FAMILY); puts_b(" SPI="); hex32(SPI_SW_MODE_ADDR);
    puts_b(" MAX_MANIFEST="); hex32(MAX_MANIFEST); puts_b("\n");
    puts_b("PMOSREC DESCRIPTOR "); puts_b(recovery_descriptor); puts_b("\n");
    puts_b("PMOSREC UART-CAP CLOCK="); put_u32_dec(uart_clock);
    puts_b(" DIV_MIN=1 DIV_MAX=65535 CURRENT="); put_u32_dec(current_rate); puts_b("\n");

    if (spi_controller_prepare() != 0 || spi_preflight(&device, id) != 0) halt();
    puts_b("PMOSREC FLASH-PREFLIGHT-OK ID="); hex8(id[0]); hex8(id[1]); hex8(id[2]);
    puts_b(" ERASE="); hex32(device->erase_size); puts_b(" PAGE="); hex32(device->page_size); puts_b("\n");
    puts_b("PMOSREC COMMAND-READY 3\n");

    for (;;) {
        if (!recv_line_timeout(line, sizeof(line), PACKAGE_HEADER_TIMEOUT_MS)) {
            puts_b("PMOSREC COMMAND-IDLE\n");
            continue;
        }
        count = split_tokens(line, tokens, 12u);
        if (count < 2u || !text_equal(tokens[0], "PMOS3")) {
            puts_b("PMOSREC RESULT ERROR UNKNOWN-COMMAND\n"); continue;
        }
        if (text_equal(tokens[1], "BAUD-OFFER")) {
            if (handle_baud_offer(tokens, count, uart_clock, current_rate) != 0)
                puts_b("PMOS3 BAUD-REJECT INVALID\n");
            continue;
        }
        if (text_equal(tokens[1], "BAUD-PREPARE")) {
            if (run_baud_test(tokens, count, uart_clock, &current_divisor, &current_rate) < 0)
                puts_b("PMOS3 BAUD-REJECT INVALID\n");
            continue;
        }
        if (text_equal(tokens[1], "FEATURE")) {
            if (handle_feature_test(tokens, count) < 0) puts_b("PMOS3 FEATURE-REJECT INVALID\n");
            continue;
        }
        if (text_equal(tokens[1], "PREFLIGHT")) {
            if (count != 4u || !parse_u32_token(tokens[2], &address) ||
                !parse_u32_token(tokens[3], &seed)) {
                puts_b("PMOSREC RESULT ERROR PREFLIGHT-REQUEST\n"); continue;
            }
            preflight.magic0 = PREFLIGHT_MAGIC0; preflight.magic1 = PREFLIGHT_MAGIC1;
            preflight.version = PREFLIGHT_PROTOCOL_VERSION; preflight.flags = PREFLIGHT_FLAG_RESTORE;
            preflight.scratch_address = address; preflight.scratch_size = 0x10000u;
            preflight.pattern_seed = seed; preflight.header_crc32 = 0u;
            puts_b("PMOSPFT HEADER-ACK\n");
            (void)spi_scratch_preflight(device, &preflight);
            continue;
        }
        if (text_equal(tokens[1], "PACKAGE") && count == 2u) {
            puts_b("PMOS3 PACKAGE-READY\n");
            run_package_v3(device, id);
            halt();
        }
        puts_b("PMOSREC RESULT ERROR UNKNOWN-COMMAND\n");
    }
}
