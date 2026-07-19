/*
 * postmerkOS PMOSLIVE pre-kernel UART live boot payload.
 *
 * This intentionally compiles the proven PMOSREC v3 transport implementation
 * into the same translation unit. PMOSLIVE_TRANSPORT_ONLY excludes all SPI,
 * erase, program, preflight, confirmation, and flash-main code before the
 * compiler can pool its string literals; section garbage collection is a
 * secondary safeguard.
 */
#define PMOSLIVE_TRANSPORT_ONLY 1
#define recovery_main pmosrec_flash_main_unreachable
#include "../uart-firmware-recovery/recovery.c"
#undef recovery_main
#undef PMOSLIVE_TRANSPORT_ONLY

/* Clang and GCC may lower fixed-size initialisers or zeroing loops to
 * memcpy/memset even in a freestanding build. Keep both routines local so the
 * payload never acquires an implicit libc dependency. */
void *memcpy(void *destination, const void *source, unsigned long bytes)
{
    u8 *out = (u8 *)destination;
    const u8 *in = (const u8 *)source;
    unsigned long i;
    for (i = 0u; i < bytes; i++) out[i] = in[i];
    return destination;
}

void *memset(void *destination, int value, unsigned long bytes)
{
    u8 *out = (u8 *)destination;
    unsigned long i;
    for (i = 0u; i < bytes; i++) out[i] = (u8)value;
    return destination;
}

#define FLAG_LIVE_BOOT 0x00000008u
#define LIVE_KNOWN_FLAGS (FLAG_LIVE_BOOT | FLAG_DRY_RUN | FLAG_FORCE_UNTESTED)
#define SPIM_HEADER_BYTES 32u
#define LIVE_KERNEL_DEST 0x81000000u
#define LIVE_KERNEL_SLOT_BYTES 0x002c0000u
#define LIVE_ROOTFS_DEST 0x87000000u
#define LIVE_ROOTFS_MAX 0x00800000u
#define LIVE_KERNEL_MEMORY_MIB 120u
/* The decompressed MIPS kernel begins at physical 0x1000. Keep the complete
 * legacy argc/argv/envp workspace in the remainder of the first page, after
 * the exception-vector area, and access it through uncached KSEG1. */
#define LIVE_BOOT_PARAMS_PHYS_BASE 0x00000400u
#define LIVE_BOOT_PARAMS_BASE (0xa0000000u + LIVE_BOOT_PARAMS_PHYS_BASE)
#define LIVE_BOOT_PARAMS_BYTES 0x00000c00u
#define LIVE_ARGV_OFFSET 0x000u
#define LIVE_ARGV_MAX 16u
#define LIVE_ARG_STRINGS_OFFSET 0x080u
#define LIVE_ENVP_OFFSET 0x200u
#define LIVE_ENVP_MAX 8u
#define LIVE_ENV_STRINGS_OFFSET 0x240u
#if LIVE_BOOT_PARAMS_PHYS_BASE < 0x00000400u || \
    LIVE_BOOT_PARAMS_PHYS_BASE + LIVE_BOOT_PARAMS_BYTES > 0x00001000u
#error PMOSLIVE boot parameters must remain in physical 0x400-0xfff
#endif
#if LIVE_ARGV_OFFSET + LIVE_ARGV_MAX * 4u > LIVE_ARG_STRINGS_OFFSET || \
    LIVE_ARG_STRINGS_OFFSET >= LIVE_ENVP_OFFSET || \
    LIVE_ENVP_OFFSET + LIVE_ENVP_MAX * 4u > LIVE_ENV_STRINGS_OFFSET || \
    LIVE_ENV_STRINGS_OFFSET >= LIVE_BOOT_PARAMS_BYTES
#error PMOSLIVE boot-parameter subregions overlap
#endif
#define LIVE_CACHE_LINE 32u
#define SQUASHFS_MAJOR 4u
#if RECOVERY_SOC_FAMILY == 1
#define LIVE_SOC_NAME "luton26"
#define LIVE_SOC_FAMILY_ID 1u
#define LIVE_DESCRIPTOR \
    "PMOSLIVE3;SOC=luton26;FAMILY=1;PROTO=3;FLASH=0;LIVEBOOT=1;" \
    "IMAGE_BYTES=16777216;KERNEL=81000000;ROOTFS=87000000;MEM_MIB=120;" \
    "FRAME_MAX=4096;WINDOW_MAX=16;SPARSE=1;LZ4=1;END"
#elif RECOVERY_SOC_FAMILY == 2
#define LIVE_SOC_NAME "jaguar1"
#define LIVE_SOC_FAMILY_ID 2u
#define LIVE_DESCRIPTOR \
    "PMOSLIVE3;SOC=jaguar1;FAMILY=2;PROTO=3;FLASH=0;LIVEBOOT=1;" \
    "IMAGE_BYTES=16777216;KERNEL=81000000;ROOTFS=87000000;MEM_MIB=120;" \
    "FRAME_MAX=4096;WINDOW_MAX=16;SPARSE=1;LZ4=1;END"
#else
#error PMOSLIVE requires RECOVERY_SOC_FAMILY 1 (Luton26) or 2 (Jaguar1)
#endif

struct live_spim_header {
    u32 magic;
    u32 load_addr;
    u32 size;
    u32 entry_addr;
    u32 expected_crc32;
    u32 reserved0;
    u32 reserved1;
    u32 reserved2;
};

struct live_boot_plan {
    u32 kernel_size;
    u32 kernel_copy_size;
    u32 kernel_entry;
    u32 rootfs_size;
    u32 squashfs_bytes_used;
};

static const char live_descriptor[] = LIVE_DESCRIPTOR;

void liveboot_jump_linux(u32 entry, u32 argc, u32 argv, u32 envp, u32 extra)
    __attribute__((noreturn));

static u32 live_le16(const u8 *data)
{
    return (u32)data[0] | ((u32)data[1] << 8);
}

static void live_copy(u8 *destination, const u8 *source, u32 bytes)
{
    u32 i;
    for (i = 0u; i < bytes; i++) destination[i] = source[i];
}

static void live_zero(u8 *destination, u32 bytes)
{
    u32 i;
    for (i = 0u; i < bytes; i++) destination[i] = 0u;
}

static u32 live_align_up(u32 value, u32 alignment)
{
    return (value + alignment - 1u) & ~(alignment - 1u);
}

static void live_cache_prepare(u32 start, u32 size)
{
    u32 address = start & ~(LIVE_CACHE_LINE - 1u);
    u32 end = live_align_up(start + size, LIVE_CACHE_LINE);
    for (; address < end; address += LIVE_CACHE_LINE) {
        __asm__ __volatile__(
            ".set push\n\t"
            ".set noreorder\n\t"
            ".set mips32r2\n\t"
            "cache 0x15, 0(%0)\n\t"
            ".set pop\n\t" : : "r"(address) : "memory");
    }
    __asm__ __volatile__("sync" ::: "memory");
    address = start & ~(LIVE_CACHE_LINE - 1u);
    for (; address < end; address += LIVE_CACHE_LINE) {
        __asm__ __volatile__(
            ".set push\n\t"
            ".set noreorder\n\t"
            ".set mips32r2\n\t"
            "cache 0x10, 0(%0)\n\t"
            ".set pop\n\t" : : "r"(address) : "memory");
    }
    __asm__ __volatile__("sync\n\tehb" ::: "memory");
}

static void live_disable_interrupts(void)
{
    u32 status;
    __asm__ __volatile__("mfc0 %0, $12" : "=r"(status));
    status &= ~1u;
    __asm__ __volatile__("mtc0 %0, $12\n\tehb" : : "r"(status) : "memory");
}

static int live_model_allowed(const char *model)
{
#if RECOVERY_SOC_FAMILY == 1
    return text_equal(model, "MS22") || text_equal(model, "MS22P") ||
           text_equal(model, "MS220-8") || text_equal(model, "MS220-8P") ||
           text_equal(model, "MS220-24") || text_equal(model, "MS220-24P");
#else
    return text_equal(model, "MS42") || text_equal(model, "MS42P");
#endif
}

static int validate_live_header(const struct package_v3_header *header, const u8 *raw)
{
    if (header->magic0 != PKG_MAGIC0 || header->magic1 != PKG_MAGIC1 ||
        header->version != PACKAGE_PROTOCOL_VERSION ||
        (header->flags & ~LIVE_KNOWN_FLAGS) != 0u ||
        (header->flags & FLAG_LIVE_BOOT) == 0u ||
        header->soc_family != LIVE_SOC_FAMILY_ID || header->image_size != FULL_IMAGE_SIZE ||
        header->manifest_size == 0u || header->manifest_size > MAX_MANIFEST ||
        (header->frame_size != 1024u && header->frame_size != 4096u) ||
        header->window_size == 0u || header->window_size > MAX_WINDOW ||
        header->representation > V3_REPR_SPARSE_LZ4 ||
        header->image_frame_count == 0u ||
        header->image_frame_count > (header->image_size + header->frame_size - 1u) / header->frame_size ||
        header->manifest_frame_count != (header->manifest_size + header->frame_size - 1u) / header->frame_size ||
        header->image_wire_bytes == 0u ||
        header->image_wire_bytes > header->image_frame_count * (FRAME_HEADER_BYTES + MAX_WIRE_CHUNK) ||
        header->target_model[0] == 0 || !live_model_allowed(header->target_model) ||
        header->header_crc32 != pmos_crc32(raw, PACKAGE_HEADER_BYTES - 4u)) return 0;
    return 1;
}

static int validate_live_manifest(const struct package_v3_header *header,
                                  u32 *manifest_rootfs_size)
{
    char family[16];
    char status[40];
    char artifact_sha[65];
    char boot_chain[48];
    u8 image_digest_hex[64];
    const u8 *models, *artifact, *recovery, *firmware;
    u32 models_len, artifact_len, recovery_len, firmware_len;
    u32 bytes, protocol, rootfs_bytes;

    if (!json_string_value(MANIFEST_BASE, header->manifest_size,
                           "target_family", family, sizeof(family)) ||
        !text_equal(family, "vcore3")) return -1;
    if (!json_object_value(MANIFEST_BASE, header->manifest_size,
                           "models", &models, &models_len) ||
        !json_string_value(models, models_len, header->target_model,
                           status, sizeof(status))) return -1;
    if (text_equal(status, "known-incompatible") ||
        (text_equal(status, "untested") &&
         (header->flags & FLAG_FORCE_UNTESTED) == 0u)) return -1;
    if (!text_equal(status, "validated") && !text_equal(status, "confirmed") &&
        !text_equal(status, "untested")) return -1;

    if (!json_object_value(MANIFEST_BASE, header->manifest_size,
                           "artifact", &artifact, &artifact_len) ||
        !json_u32_value(artifact, artifact_len, "bytes", &bytes) ||
        bytes != FULL_IMAGE_SIZE ||
        !json_u32_value(artifact, artifact_len, "rootfs_bytes", &rootfs_bytes) ||
        rootfs_bytes == 0u || rootfs_bytes > LIVE_ROOTFS_MAX ||
        !json_string_value(artifact, artifact_len, "sha256",
                           artifact_sha, sizeof(artifact_sha)) ||
        !json_string_value(artifact, artifact_len, "boot_chain",
                           boot_chain, sizeof(boot_chain)) ||
        !text_equal(boot_chain, "vcoreiii-linuxloader-spim-v2")) return -1;
    digest_to_hex(header->image_sha256, image_digest_hex);
    if (!bytes_equal((const u8 *)artifact_sha, image_digest_hex, 64u) ||
        artifact_sha[64] != 0) return -1;

    /* Reuse a normal retail manifest. It only needs to assert the proven v3
     * transport contract; PMOSLIVE deliberately ignores all flash geometry. */
    if (!json_object_value(MANIFEST_BASE, header->manifest_size,
                           "recovery", &recovery, &recovery_len) ||
        !json_object_value(recovery, recovery_len,
                           "uart_firmware", &firmware, &firmware_len) ||
        !json_u32_value(firmware, firmware_len, "protocol_version", &protocol) ||
        protocol != PACKAGE_PROTOCOL_VERSION ||
        !json_u32_value(firmware, firmware_len, "full_image_bytes", &bytes) ||
        bytes != FULL_IMAGE_SIZE ||
        !json_array_contains_string(firmware, firmware_len,
                                    "transport_integrity", "frame-crc32") ||
        !json_array_contains_string(firmware, firmware_len,
                                    "transport_integrity", "compact-ack-crc32") ||
        !json_array_contains_string(firmware, firmware_len,
                                    "transport_integrity", "object-crc32") ||
        !json_array_contains_string(firmware, firmware_len,
                                    "transport_integrity", "object-sha256") ||
        !json_array_contains_string(firmware, firmware_len,
                                    "transport_integrity", "reconstructed-image-sha256")) return -1;

    *manifest_rootfs_size = rootfs_bytes;
    return 0;
}

static int prepare_live_image(u32 manifest_rootfs_size, struct live_boot_plan *plan)
{
    const u8 *kernel_region = STAGING_BASE + KERNEL_OFFSET;
    const u8 *kernel_payload = kernel_region + SPIM_HEADER_BYTES;
    const u8 *rootfs = STAGING_BASE + ROOTFS_OFFSET;
    struct live_spim_header header;
    u8 crc_header[SPIM_HEADER_BYTES];
    u32 i, crc, copy_size, squashfs_bytes_used, rootfs_end;

    for (i = 0u; i < SPIM_HEADER_BYTES; i++) crc_header[i] = kernel_region[i];
    header.magic = le32(kernel_region + 0u);
    header.load_addr = le32(kernel_region + 4u);
    header.size = le32(kernel_region + 8u);
    header.entry_addr = le32(kernel_region + 12u);
    header.expected_crc32 = le32(kernel_region + 16u);
    header.reserved0 = le32(kernel_region + 20u);
    header.reserved1 = le32(kernel_region + 24u);
    header.reserved2 = le32(kernel_region + 28u);

    if (header.magic != 0x4d495053u || header.load_addr != LIVE_KERNEL_DEST ||
        header.entry_addr < header.load_addr ||
        header.size == 0u || header.size > LIVE_KERNEL_SLOT_BYTES - SPIM_HEADER_BYTES) {
        puts_b("PMOSLIVE RESULT ERROR SPIM-HEADER\n"); return -1;
    }
    copy_size = live_align_up(header.size, 32u);
    if (header.entry_addr >= header.load_addr + copy_size ||
        copy_size > LIVE_KERNEL_SLOT_BYTES - SPIM_HEADER_BYTES) {
        puts_b("PMOSLIVE RESULT ERROR SPIM-GEOMETRY\n"); return -1;
    }
    crc_header[16] = 0u; crc_header[17] = 0u;
    crc_header[18] = 0u; crc_header[19] = 0u;
    crc = pmos_crc32_update(0xffffffffu, crc_header, SPIM_HEADER_BYTES);
    crc = pmos_crc32_update(crc, kernel_payload, copy_size);
    crc = ~crc;
    if (crc != header.expected_crc32) {
        puts_b("PMOSLIVE RESULT ERROR SPIM-CRC EXPECTED="); hex32(header.expected_crc32);
        puts_b(" GOT="); hex32(crc); puts_b("\n"); return -1;
    }

    if (!bytes_equal(rootfs, (const u8 *)"hsqs", 4u) ||
        live_le16(rootfs + 28u) != SQUASHFS_MAJOR) {
        puts_b("PMOSLIVE RESULT ERROR SQUASHFS-SUPERBLOCK\n"); return -1;
    }
    if (le32(rootfs + 44u) != 0u) {
        puts_b("PMOSLIVE RESULT ERROR SQUASHFS-TOO-LARGE\n"); return -1;
    }
    squashfs_bytes_used = le32(rootfs + 40u);
    if (squashfs_bytes_used < 96u || squashfs_bytes_used > manifest_rootfs_size ||
        manifest_rootfs_size > LIVE_ROOTFS_MAX) {
        puts_b("PMOSLIVE RESULT ERROR SQUASHFS-SIZE MANIFEST=");
        hex32(manifest_rootfs_size); puts_b(" USED="); hex32(squashfs_bytes_used);
        puts_b("\n"); return -1;
    }
    rootfs_end = LIVE_ROOTFS_DEST + manifest_rootfs_size;
    if (rootfs_end < LIVE_ROOTFS_DEST || rootfs_end > 0x87800000u) {
        puts_b("PMOSLIVE RESULT ERROR ROOTFS-RANGE\n"); return -1;
    }

    puts_b("PMOSLIVE SPIM-VERIFIED LOAD="); hex32(header.load_addr);
    puts_b(" ENTRY="); hex32(header.entry_addr); puts_b(" DECLARED=");
    hex32(header.size); puts_b(" COPIED="); hex32(copy_size); puts_b("\n");
    puts_b("PMOSLIVE SQUASHFS-VERIFIED SOURCE="); hex32((u32)rootfs);
    puts_b(" DEST="); hex32(LIVE_ROOTFS_DEST); puts_b(" IMAGE=");
    hex32(manifest_rootfs_size); puts_b(" USED="); hex32(squashfs_bytes_used);
    puts_b("\n");

    live_copy((u8 *)LIVE_KERNEL_DEST, kernel_payload, copy_size);
    live_copy((u8 *)LIVE_ROOTFS_DEST, rootfs, manifest_rootfs_size);
    live_cache_prepare(LIVE_KERNEL_DEST, copy_size);
    live_cache_prepare(LIVE_ROOTFS_DEST, manifest_rootfs_size);

    plan->kernel_size = header.size;
    plan->kernel_copy_size = copy_size;
    plan->kernel_entry = header.entry_addr;
    plan->rootfs_size = manifest_rootfs_size;
    plan->squashfs_bytes_used = squashfs_bytes_used;
    return 0;
}

static char *live_put_string(char *destination, const char *text)
{
    while (*text) *destination++ = *text++;
    *destination++ = 0;
    return destination;
}

static char *live_put_hex_value(char *destination, u32 value)
{
    static const char digits[] = "0123456789abcdef";
    int shift;
    *destination++ = '0'; *destination++ = 'x';
    for (shift = 28; shift >= 0; shift -= 4)
        *destination++ = digits[(value >> shift) & 0xfu];
    *destination++ = 0;
    return destination;
}

static u32 live_text_bytes(const char *text)
{
    u32 bytes = 1u;
    while (*text++) bytes++;
    return bytes;
}

static int live_add_arg(u32 *argv, u32 *argc, char **string_cursor,
                        char *string_limit, const char *text)
{
    u32 bytes = live_text_bytes(text);
    if (*argc >= LIVE_ARGV_MAX - 1u ||
        (u32)*string_cursor > (u32)string_limit ||
        bytes > (u32)string_limit - (u32)*string_cursor) return -1;
    argv[*argc] = (u32)*string_cursor;
    *string_cursor = live_put_string(*string_cursor, text);
    (*argc)++;
    return 0;
}

static int live_add_env(u32 *envp, u32 *index, char **string_cursor,
                        char *string_limit, const char *name,
                        u32 value, u32 decimal)
{
    char *start = *string_cursor;
    char decimal_buffer[16];
    u32 count = 0u, i, value_bytes;
    u32 name_bytes = live_text_bytes(name) - 1u;
    u32 original_value = value;

    if (decimal) {
        if (value == 0u) count = 1u;
        while (value != 0u) {
            count++;
            value /= 10u;
        }
        value_bytes = count + 1u;
    } else {
        value_bytes = 11u; /* 0x + eight hex digits + NUL */
    }
    if (*index >= LIVE_ENVP_MAX - 1u ||
        (u32)start > (u32)string_limit ||
        name_bytes + 1u + value_bytes > (u32)string_limit - (u32)start) return -1;

    value = original_value;
    count = 0u;
    envp[*index] = (u32)start;
    *string_cursor = live_put_string(*string_cursor, name) - 1;
    *(*string_cursor)++ = '=';
    if (decimal) {
        if (value == 0u) decimal_buffer[count++] = '0';
        while (value != 0u) {
            decimal_buffer[count++] = (char)('0' + value % 10u);
            value /= 10u;
        }
        for (i = 0u; i < count; i++) *(*string_cursor)++ = decimal_buffer[count - i - 1u];
        *(*string_cursor)++ = 0;
    } else {
        *string_cursor = live_put_hex_value(*string_cursor, value);
    }
    (*index)++;
    return 0;
}

static int live_make_model_arg(char *buffer, u32 bytes, const char *model)
{
    static const char prefix[] = "postmerkos.model=";
    u32 used = 0u, i = 0u;
    while (prefix[i]) {
        if (used + 1u >= bytes) return -1;
        buffer[used++] = prefix[i++];
    }
    i = 0u;
    while (model[i]) {
        if (used + 1u >= bytes) return -1;
        buffer[used++] = model[i++];
    }
    buffer[used] = 0;
    return 0;
}

static int prepare_linux_boot_params(const struct live_boot_plan *plan,
                                     const char *target_model,
                                     u32 *argc_out, u32 *argv_address,
                                     u32 *envp_address)
{
    u32 *argv = (u32 *)(LIVE_BOOT_PARAMS_BASE + LIVE_ARGV_OFFSET);
    u32 *envp = (u32 *)(LIVE_BOOT_PARAMS_BASE + LIVE_ENVP_OFFSET);
    char *arg_strings = (char *)(LIVE_BOOT_PARAMS_BASE + LIVE_ARG_STRINGS_OFFSET);
    char *env_strings = (char *)(LIVE_BOOT_PARAMS_BASE + LIVE_ENV_STRINGS_OFFSET);
    char *arg_limit = (char *)(LIVE_BOOT_PARAMS_BASE + LIVE_ENVP_OFFSET);
    char *env_limit = (char *)(LIVE_BOOT_PARAMS_BASE + LIVE_BOOT_PARAMS_BYTES);
    char rd_start[24] = "rd_start=";
    char rd_size[24] = "rd_size=";
    char model_arg[48];
    char *cursor;
    u32 argc = 1u, envc = 0u;

    live_zero((u8 *)LIVE_BOOT_PARAMS_BASE, LIVE_BOOT_PARAMS_BYTES);
    argv[0] = 0u;
#define LIVE_ADD_ARG(text) \
    do { if (live_add_arg(argv, &argc, &arg_strings, arg_limit, (text)) != 0) \
        goto overflow; } while (0)
#define LIVE_ADD_ENV(name, value, decimal) \
    do { if (live_add_env(envp, &envc, &env_strings, env_limit, \
                          (name), (value), (decimal)) != 0) goto overflow; } while (0)

    LIVE_ADD_ARG("console=ttyS0,115200n8");
    LIVE_ADD_ARG("mem=120M");
    cursor = rd_start + 9;
    (void)live_put_hex_value(cursor, LIVE_ROOTFS_DEST);
    LIVE_ADD_ARG(rd_start);
    cursor = rd_size + 8;
    (void)live_put_hex_value(cursor, plan->rootfs_size);
    LIVE_ADD_ARG(rd_size);
    LIVE_ADD_ARG("root=/dev/ram0");
    LIVE_ADD_ARG("rootfstype=squashfs");
    LIVE_ADD_ARG("ro");
    LIVE_ADD_ARG("load_ramdisk=1");
    LIVE_ADD_ARG("prompt_ramdisk=0");
    LIVE_ADD_ARG("ramdisk_size=8192");
    LIVE_ADD_ARG("postmerkos.live=1");
    if (!live_model_allowed(target_model) ||
        live_make_model_arg(model_arg, sizeof(model_arg), target_model) != 0)
        goto overflow;
    LIVE_ADD_ARG(model_arg);
    LIVE_ADD_ARG("panic=5");
    argv[argc] = 0u;

    LIVE_ADD_ENV("memsize", LIVE_KERNEL_MEMORY_MIB, 1u);
    LIVE_ADD_ENV("initrd_start", 0xa7000000u, 0u);
    LIVE_ADD_ENV("initrd_size", plan->rootfs_size, 0u);
    envp[envc] = 0u;

    /* Match U-Boot's MIPS legacy ABI: argv/envp and their strings are passed
     * through the uncached KSEG1 alias of physical 0x400-0xfff. The first
     * 0x400 bytes remain available for exception vectors, while the kernel's
     * decompressed image begins at physical 0x1000. */
    __asm__ __volatile__("sync" ::: "memory");
    *argc_out = argc;
    *argv_address = (u32)argv;
    *envp_address = (u32)envp;
    return 0;

overflow:
    puts_b("PMOSLIVE RESULT ERROR BOOT-PARAMS-OVERFLOW\n");
    return -1;
#undef LIVE_ADD_ARG
#undef LIVE_ADD_ENV
}

static int read_boot_confirmation(u32 nonce)
{
    char line[64];
    char expected[32];
    static const char prefix[] = "BOOTRAM ";
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

static void launch_live_linux(const struct live_boot_plan *plan,
                              const char *target_model,
                              u32 baseline_divisor)
{
    u32 argv_address, envp_address, argc, i;
    if (prepare_linux_boot_params(plan, target_model, &argc, &argv_address, &envp_address) != 0) {
        puts_b("PMOSLIVE HALT BOOT-PARAMS\n");
        for (;;) __asm__ __volatile__("wait");
    }
    puts_b("PMOSLIVE BOOT-PLAN ENTRY="); hex32(plan->kernel_entry);
    puts_b(" ARGC="); put_u32_dec(argc); puts_b(" ARGV="); hex32(argv_address);
    puts_b(" ENVP="); hex32(envp_address); puts_b(" ROOTFS=");
    hex32(LIVE_ROOTFS_DEST); puts_b(" ROOTFS_BYTES="); hex32(plan->rootfs_size);
    puts_b(" MODEL="); puts_b(target_model);
    puts_b(" MEM_MIB=120 BOOTPARAM_PHYS="); hex32(LIVE_BOOT_PARAMS_PHYS_BASE);
    puts_b(" BOOTPARAM_BYTES="); hex32(LIVE_BOOT_PARAMS_BYTES); puts_b("\n");
    puts_b("PMOSLIVE UART-RESTORE RATE=115200 DIV="); put_u32_dec(baseline_divisor);
    puts_b("\n");
    uart_wait_tx_empty();
    sleep_ms(50u);
    uart_set_divisor(baseline_divisor);
    for (i = 0u; i < 10u; i++) {
        puts_b("PMOSLIVE UART-BASELINE-READY RATE=115200\n");
        sleep_ms(50u);
    }
    puts_b("PMOSLIVE EXEC ENTRY="); hex32(plan->kernel_entry); puts_b("\n");
    uart_wait_tx_empty();
    live_disable_interrupts();
    __asm__ __volatile__("sync\n\tehb" ::: "memory");
    liveboot_jump_linux(plan->kernel_entry, argc, argv_address, envp_address, 0u);
}

static int run_live_package_v3(u32 baseline_divisor)
{
    u8 raw[PACKAGE_HEADER_BYTES];
    struct package_v3_header header;
    struct live_boot_plan plan;
    u32 rootfs_size = 0u, nonce;

    if (!recv_exact(raw, sizeof(raw), PACKAGE_HEADER_TIMEOUT_MS)) {
        puts_b("PMOSLIVE RESULT ERROR PACKAGE-HEADER-TIMEOUT\n"); return -1;
    }
    parse_v3_package_header(&header, raw);
    if (!validate_live_header(&header, raw)) {
        puts_b("PMOSLIVE RESULT ERROR INVALID-PACKAGE-HEADER\n"); return -1;
    }
    puts_b("PMOS3 LIVEBOOT-HEADER-ACK MODEL="); puts_b(header.target_model);
    puts_b(" FRAME="); put_u32_dec(header.frame_size);
    puts_b(" WINDOW="); put_u32_dec(header.window_size);
    puts_b(" REPRESENTATION="); put_u32_dec(header.representation); puts_b("\n");

    if (receive_v3_object(MANIFEST_BASE, header.manifest_size, OBJECT_MANIFEST,
                          header.manifest_frame_count, header.frame_size,
                          header.window_size, 0u, header.manifest_crc32,
                          header.manifest_sha256, 1u) != 0) {
        puts_b("PMOSLIVE RESULT ERROR MANIFEST-TRANSFER CODE=");
        put_u32_dec(v3_last_frame_error); puts_b("\n"); return -1;
    }
    puts_b("PMOS3 MANIFEST-OBJECT-VERIFIED\n");
    if (validate_live_manifest(&header, &rootfs_size) != 0) {
        puts_b("PMOSLIVE RESULT ERROR MANIFEST-FIRST-VALIDATION\n"); return -1;
    }
    puts_b("PMOS3 MANIFEST-ACCEPTED ROOTFS_BYTES="); hex32(rootfs_size); puts_b("\n");

    if (receive_v3_object(STAGING_BASE, header.image_size, OBJECT_IMAGE,
                          header.image_frame_count, header.frame_size,
                          header.window_size, 1u, header.image_crc32,
                          header.image_sha256, 1u) != 0) {
        puts_b("PMOSLIVE RESULT ERROR IMAGE-TRANSFER CODE=");
        put_u32_dec(v3_last_frame_error); puts_b("\n"); return -1;
    }
    puts_b("PMOS3 IMAGE-OBJECT-VERIFIED\n");
    if (prepare_live_image(rootfs_size, &plan) != 0) return -1;
    puts_b("PMOSLIVE RESULT IMAGE-READY\n");

    if ((header.flags & FLAG_DRY_RUN) != 0u) {
        puts_b("PMOSLIVE RESULT DRY-RUN-OK\n"); return 0;
    }
    nonce = header.image_crc32 ^ header.manifest_crc32 ^ cp0_count() ^
            plan.kernel_entry ^ plan.rootfs_size;
    puts_b("PMOSLIVE BOOT-CHALLENGE "); hex32(nonce); puts_b("\n");
    puts_b("PMOSLIVE CONFIRMATION-WAIT ENTER=BOOTRAM+"); hex32(nonce);
    puts_b(" POWER-CYCLE-TO-CANCEL\n");
    while (read_boot_confirmation(nonce) != 0) {
        puts_b("PMOSLIVE CONFIRMATION-REJECTED EXPECTED=BOOTRAM ");
        hex32(nonce); puts_b(" RETRY=FOREVER POWER-CYCLE-TO-CANCEL\n");
    }
    puts_b("PMOSLIVE CONFIRMATION-ACK\n");
    launch_live_linux(&plan, header.target_model, baseline_divisor);
    return 0;
}

void liveboot_main(void)
{
    char line[192];
    char *tokens[12];
    u32 count, baseline_divisor, current_divisor, uart_clock, current_rate;

    baseline_divisor = uart_get_divisor();
    if (baseline_divisor == 0u) baseline_divisor = 56u;
    current_divisor = baseline_divisor;
    uart_clock = baseline_divisor * 16u * UART_BASELINE_BAUD;
    current_rate = uart_actual_rate(uart_clock, current_divisor);

    puts_b("PMOSLIVE READY 3 SOC="); puts_b(LIVE_SOC_NAME);
    puts_b(" FAMILY="); hex32(LIVE_SOC_FAMILY_ID); puts_b(" FLASH=0\n");
    puts_b("PMOSLIVE DESCRIPTOR "); puts_b(live_descriptor); puts_b("\n");
    puts_b("PMOSLIVE RAM-MAP STAGING=81400000-82400000 MANIFEST=82400000 ");
    puts_b("EXEC=86c00000-87000000 ROOTFS=87000000-87800000\n");
    puts_b("PMOSLIVE UART-CAP CLOCK="); put_u32_dec(uart_clock);
    puts_b(" DIV_MIN=1 DIV_MAX=65535 CURRENT="); put_u32_dec(current_rate); puts_b("\n");
    puts_b("PMOSLIVE COMMAND-READY 3\n");

    for (;;) {
        if (!recv_line_timeout(line, sizeof(line), PACKAGE_HEADER_TIMEOUT_MS)) {
            puts_b("PMOSLIVE COMMAND-IDLE\n"); continue;
        }
        count = split_tokens(line, tokens, 12u);
        if (count < 2u || !text_equal(tokens[0], "PMOS3")) {
            puts_b("PMOSLIVE RESULT ERROR UNKNOWN-COMMAND\n"); continue;
        }
        if (text_equal(tokens[1], "BAUD-OFFER")) {
            if (handle_baud_offer(tokens, count, uart_clock, current_rate) != 0)
                puts_b("PMOS3 BAUD-REJECT INVALID\n");
            continue;
        }
        if (text_equal(tokens[1], "BAUD-PREPARE")) {
            if (run_baud_test(tokens, count, uart_clock,
                              &current_divisor, &current_rate) < 0)
                puts_b("PMOS3 BAUD-REJECT INVALID\n");
            continue;
        }
        if (text_equal(tokens[1], "FEATURE")) {
            if (handle_feature_test(tokens, count) < 0)
                puts_b("PMOS3 FEATURE-REJECT INVALID\n");
            continue;
        }
        if (text_equal(tokens[1], "LIVEBOOT") && count == 2u) {
            puts_b("PMOS3 LIVEBOOT-READY\n");
            (void)run_live_package_v3(baseline_divisor);
            puts_b("PMOSLIVE COMMAND-READY 3\n");
            continue;
        }
        puts_b("PMOSLIVE RESULT ERROR UNKNOWN-COMMAND\n");
    }
}
