/*
 * Hosted tests for the portable null_kernel AMD64 support logic.
 *
 * These exercise the exact same shared C objects the kernel builds, with
 * tiny hand-built ELF/ACPI/TAR images and RAM-disk/VFS callbacks. No QEMU or
 * boot loader is required; main() returns non-zero on any failed check.
 */
#include <stdio.h>
#include <string.h>

#include "nullk/printk.h"
#include "nullk/vfs.h"
#include "nullk/elf64.h"
#include "nullk/acpi.h"
#include "nullk/sandbox.h"
#include "nullk/shell64.h"
#include "nullk/usb_as_sata.h"
#include "nullk/abi.h"

static int g_checks;
static int g_passed;

#define CHECK(name, expr)                                                    \
    do {                                                                     \
        g_checks++;                                                          \
        if (expr) {                                                          \
            g_passed++;                                                      \
            printf("PASS %s\n", name);                                       \
        } else {                                                             \
            printf("FAIL %s (line %d)\n", name, __LINE__);                   \
        }                                                                    \
    } while (0)

/* ------------------------------------------------------------------ */
/* Byte writers used to synthesise binary images.                      */
/* ------------------------------------------------------------------ */

static void put16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
}

static void put32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

static void put64(uint8_t *p, uint64_t v)
{
    put32(p, (uint32_t)v);
    put32(p + 4, (uint32_t)(v >> 32));
}

/* Set an ACPI checksum field so the sum of len bytes becomes zero. */
static void fix_checksum(uint8_t *p, uint32_t len, uint32_t offset)
{
    uint8_t sum;
    uint32_t i;

    p[offset] = 0u;
    sum = 0u;
    for (i = 0u; i < len; i++) {
        sum = (uint8_t)(sum + p[i]);
    }
    p[offset] = (uint8_t)(0u - sum);
}

/* ------------------------------------------------------------------ */
/* printk.                                                             */
/* ------------------------------------------------------------------ */

static char g_log[1024];
static uint32_t g_log_len;

static void log_reset(void)
{
    g_log_len = 0u;
    g_log[0] = '\0';
}

static void capture_sink(const char *text, uint32_t len)
{
    uint32_t i;

    for (i = 0u; (i < len) && ((g_log_len + 1u) < sizeof(g_log)); i++) {
        g_log[g_log_len++] = text[i];
    }
    g_log[g_log_len] = '\0';
}

static void test_printk(void)
{
    static const char expected_error[] = "null_kernel: error: bad 7\n";
    static const char expected_info[] = "null_kernel: ready\n";
    char buf[128];
    char tiny[4];
    int n;

    n = nk_snprintf(buf, (uint32_t)sizeof(buf),
                    "%s %d %u %x %08x %p %%", "hi", -42, 42u, 0xabcu,
                    0x23u, (void *)(uintptr_t)0x1234u);
    CHECK("printk basic format",
          (n == 31) && (strcmp(buf, "hi -42 42 abc 00000023 0x1234 %") == 0));

    n = nk_snprintf(buf, (uint32_t)sizeof(buf), "%llu %llx %lu %lx",
                    (unsigned long long)1234567890123ULL,
                    (unsigned long long)0xabcdefULL, (unsigned long)77u,
                    (unsigned long)0xffu);
    CHECK("printk 64-bit modifiers",
          (strcmp(buf, "1234567890123 abcdef 77 ff") == 0) && (n == 26));

    n = nk_snprintf(tiny, (uint32_t)sizeof(tiny), "%s", "abcdef");
    CHECK("printk bounded output",
          (strcmp(tiny, "abc") == 0) && (n == 6));

    nk_printk_init(capture_sink);
    nk_printk_set_level(NK_KERN_INFO);
    log_reset();
    n = nk_printk(NK_KERN_ERR, "bad %d", 7);
    CHECK("printk level emit",
          (n == (int)(sizeof(expected_error) - 1u)) &&
          (strcmp(g_log, expected_error) == 0));

    log_reset();
    n = nk_printk(NK_KERN_INFO, "ready\n");
    CHECK("printk single newline",
          (n == (int)(sizeof(expected_info) - 1u)) &&
          (strcmp(g_log, expected_info) == 0));

    log_reset();
    n = nk_printk(NK_KERN_DEBUG, "quiet");
    CHECK("printk level filter", (n == 0) && (g_log_len == 0u));

    CHECK("printk sink installed", nk_printk_sink() == capture_sink);
    CHECK("printk level getter", nk_printk_level() == NK_KERN_INFO);
    nk_printk_init((nk_print_sink_t)0); /* restore the no-op default */
}

/* ------------------------------------------------------------------ */
/* VFS.                                                                */
/* ------------------------------------------------------------------ */

static void test_vfs(void)
{
    char norm[NK_VFS_PATH_MAX];
    char buf[64];
    uint32_t len = 0u;
    uint32_t nodes = 0u;
    uint32_t bytes = 0u;
    uint32_t count = 0u;
    nk_vfs_stat_t st;
    nk_vfs_dirent_t de[8];

    nk_vfs_init();

    CHECK("vfs normalize empty",
          (nk_vfs_normalize("", norm, sizeof(norm)) == NK_STATUS_OK) &&
          (strcmp(norm, "@/") == 0));
    CHECK("vfs normalize @",
          (nk_vfs_normalize("@", norm, sizeof(norm)) == NK_STATUS_OK) &&
          (strcmp(norm, "@/") == 0));
    CHECK("vfs normalize slash",
          (nk_vfs_normalize("/", norm, sizeof(norm)) == NK_STATUS_OK) &&
          (strcmp(norm, "@/") == 0));
    CHECK("vfs normalize @foo",
          (nk_vfs_normalize("@foo", norm, sizeof(norm)) == NK_STATUS_OK) &&
          (strcmp(norm, "@/foo") == 0));
    CHECK("vfs normalize dots",
          (nk_vfs_normalize("a/./b/../c", norm, sizeof(norm)) == NK_STATUS_OK) &&
          (strcmp(norm, "@/a/c") == 0));
    CHECK("vfs normalize double slash",
          (nk_vfs_normalize("@//data//system", norm, sizeof(norm)) == NK_STATUS_OK) &&
          (strcmp(norm, "@/data/system") == 0));
    CHECK("vfs normalize escape",
          nk_vfs_normalize("../x", norm, sizeof(norm)) ==
              NK_STATUS_INVALID_ARGUMENT);

    CHECK("vfs mkdir", (nk_vfs_mkdir("@/test") == NK_STATUS_OK) &&
                           (nk_vfs_mkdir("@/test/sub") == NK_STATUS_OK));
    CHECK("vfs write", nk_vfs_write_file("@/test/sub/f.txt", "hello", 5u) ==
                           NK_STATUS_OK);
    CHECK("vfs read offset",
          (nk_vfs_read_file("@/test/sub/f.txt", 2u, buf, 3u, &len) ==
           NK_STATUS_OK) &&
          (len == 3u) && (memcmp(buf, "llo", 3u) == 0));
    CHECK("vfs stat",
          (nk_vfs_stat("@/test/sub/f.txt", &st) == NK_STATUS_OK) &&
          (st.type == NK_VFS_FILE) && (st.size == 5u) &&
          (strcmp(st.name, "f.txt") == 0));
    CHECK("vfs overwrite",
          (nk_vfs_write_file("@/test/sub/f.txt", "hey", 3u) == NK_STATUS_OK) &&
          (nk_vfs_stat("@/test/sub/f.txt", &st) == NK_STATUS_OK) &&
          (st.size == 3u));
    CHECK("vfs append",
          (nk_vfs_append_file("@/test/sub/f.txt", " there", 6u) ==
           NK_STATUS_OK));
    len = 0u;
    CHECK("vfs read appended",
          (nk_vfs_read_file("@/test/sub/f.txt", 0u, buf, (uint32_t)sizeof(buf),
                            &len) == NK_STATUS_OK) &&
          (len == 9u) && (memcmp(buf, "hey there", 9u) == 0));
    CHECK("vfs readdir",
          (nk_vfs_readdir("@/test", de, 8u, &count) == NK_STATUS_OK) &&
          (count == 1u) && (de[0].type == NK_VFS_DIR) &&
          (strcmp(de[0].name, "sub") == 0));
    CHECK("vfs write creates dirs",
          (nk_vfs_write_file("@/new/deep/x", "X", 1u) == NK_STATUS_OK) &&
          (nk_vfs_stat("@/new/deep/x", &st) == NK_STATUS_OK) &&
          (st.size == 1u));
    CHECK("vfs unlink file",
          (nk_vfs_unlink("@/test/sub/f.txt") == NK_STATUS_OK) &&
          (nk_vfs_stat("@/test/sub/f.txt", &st) == NK_STATUS_NOT_FOUND));
    CHECK("vfs mkdir empty", nk_vfs_mkdir("@/empty") == NK_STATUS_OK);
    CHECK("vfs unlink empty dir", nk_vfs_unlink("@/empty") == NK_STATUS_OK);
    CHECK("vfs unlink nonempty dir",
          nk_vfs_unlink("@/test") == NK_STATUS_BUSY);
    nk_vfs_usage(&nodes, &bytes);
    CHECK("vfs usage", (nodes > 4u) && (bytes > 0u));
}

static uint32_t tar_add(uint8_t *tar, uint32_t off, const char *name,
                        const char *data, uint32_t size, char type)
{
    uint32_t i;
    uint32_t value = size;
    uint32_t sum = 0u;
    char oct[12];
    char cs[8];
    uint32_t k;

    for (i = 0u; i < 512u; i++) {
        tar[off + i] = 0u;
    }
    for (i = 0u; (name[i] != '\0') && (i < 100u); i++) {
        tar[off + i] = (uint8_t)name[i];
    }
    oct[11] = '\0';
    k = 11u;
    while (k > 0u) {
        oct[--k] = (char)('0' + (value & 7u));
        value >>= 3;
    }
    for (i = 0u; i < 12u; i++) {
        tar[off + 124u + i] = (uint8_t)oct[i];
    }
    tar[off + 156u] = (uint8_t)type;
    for (i = 0u; i < 5u; i++) {
        tar[off + 257u + i] = (uint8_t)"ustar"[i];
    }
    tar[off + 263u] = '0';
    tar[off + 264u] = '0';
    for (i = 0u; i < 8u; i++) {
        tar[off + 148u + i] = ' ';
    }
    for (i = 0u; i < 512u; i++) {
        sum += tar[off + i];
    }
    for (k = 0u; k < 6u; k++) {
        cs[5u - k] = (char)('0' + ((sum >> (3u * k)) & 7u));
    }
    cs[6] = '\0';
    cs[7] = ' ';
    for (i = 0u; i < 8u; i++) {
        tar[off + 148u + i] = (uint8_t)cs[i];
    }
    if (size != 0u) {
        memcpy(tar + off + 512u, data, size);
    }
    return off + 512u + (((size + 511u) / 512u) * 512u);
}

static void test_vfs_tar(void)
{
    uint8_t tar[4096];
    nk_vfs_stat_t st;
    char buf[16];
    uint32_t len = 0u;
    uint32_t off = 0u;

    memset(tar, 0, sizeof(tar));
    off = tar_add(tar, off, "dirA/", (const char *)0, 0u, '5');
    off = tar_add(tar, off, "dirA/one.txt", "ONE", 3u, '0');
    off = tar_add(tar, off, "./two.txt", "TWO!", 4u, '0');

    CHECK("vfs tar load", nk_vfs_load_tar(tar, off + 512u) == NK_STATUS_OK);
    CHECK("vfs tar dir", (nk_vfs_stat("@/dirA", &st) == NK_STATUS_OK) &&
                             (st.type == NK_VFS_DIR));
    CHECK("vfs tar file 1",
          (nk_vfs_read_file("@/dirA/one.txt", 0u, buf, sizeof(buf), &len) ==
           NK_STATUS_OK) &&
          (len == 3u) && (memcmp(buf, "ONE", 3u) == 0));
    CHECK("vfs tar file 2",
          (nk_vfs_read_file("@/two.txt", 0u, buf, sizeof(buf), &len) ==
           NK_STATUS_OK) &&
          (len == 4u) && (memcmp(buf, "TWO!", 4u) == 0));
}

/* ------------------------------------------------------------------ */
/* ELF64.                                                              */
/* ------------------------------------------------------------------ */

static void test_elf64(void)
{
    uint8_t image[256];
    uint8_t dest[32];
    nk_elf64_info_t info;
    uint64_t entry = 0u;
    uint8_t saved;

    memset(image, 0, sizeof(image));
    image[0] = NK_ELF64_MAGIC0;
    image[1] = 'E';
    image[2] = 'L';
    image[3] = 'F';
    image[4] = NK_ELF64_CLASS64;
    image[5] = NK_ELF64_DATA_LSB;
    image[6] = 1u;
    put16(image + 16, NK_ELF64_ET_EXEC);
    put16(image + 18, NK_ELF64_EM_X86_64);
    put32(image + 20, 1u);
    put64(image + 24, 0x1000u); /* entry */
    put64(image + 32, 64u);     /* e_phoff */
    put16(image + 52, 64u);     /* e_ehsize */
    put16(image + 54, 56u);     /* e_phentsize */
    put16(image + 56, 1u);      /* e_phnum */
    put32(image + 64, NK_ELF64_PT_LOAD);
    put64(image + 64 + 8, 120u);  /* p_offset */
    put64(image + 64 + 16, 0x1000u); /* p_vaddr */
    put64(image + 64 + 32, 4u);  /* p_filesz */
    put64(image + 64 + 40, 8u);  /* p_memsz */
    memcpy(image + 120, "ABCD", 4u);
    memset(dest, 0xcc, sizeof(dest));

    CHECK("elf probe", nk_elf64_probe(image, sizeof(image), &info) == 0);
    CHECK("elf probe fields",
          (info.machine == NK_ELF64_EM_X86_64) &&
          (info.type == NK_ELF64_ET_EXEC) && (info.entry == 0x1000u) &&
          (info.vaddr_lo == 0x1000u) && (info.vaddr_hi == 0x1008u) &&
          (info.segment_count == 1u));
    CHECK("elf load", nk_elf64_load(image, sizeof(image), dest,
                                    (uint64_t)sizeof(dest), &entry) == 0);
    CHECK("elf load result",
          (entry == 0x1000u) && (memcmp(dest, "ABCD", 4u) == 0) &&
          (dest[4] == 0u) && (dest[7] == 0u));

    saved = image[0];
    image[0] = 0u;
    CHECK("elf bad magic",
          nk_elf64_probe(image, sizeof(image), &info) < 0);
    image[0] = saved;
    CHECK("elf check range ok", nk_elf64_check_range(10u, 5u, 15u) == 0);
    CHECK("elf check range overflow",
          nk_elf64_check_range(10u, 6u, 15u) < 0);
    CHECK("elf check range base",
          nk_elf64_check_range(16u, 0u, 15u) < 0);
}

/* ------------------------------------------------------------------ */
/* ACPI.                                                               */
/* ------------------------------------------------------------------ */

static void test_acpi(void)
{
    static uint8_t rsdp[36];
    static uint8_t rsdt[44];
    static uint8_t xsdt[52];
    static uint8_t fadt[68];
    static uint8_t madt[64];
    nk_acpi_info_t info;
    uint32_t sum = 0u;
    uint32_t pos;

    memset(&info, 0, sizeof(info));
    memset(rsdp, 0, sizeof(rsdp));
    memset(rsdt, 0, sizeof(rsdt));
    memset(xsdt, 0, sizeof(xsdt));
    memset(fadt, 0, sizeof(fadt));
    memset(madt, 0, sizeof(madt));

    CHECK("acpi checksum helper", nk_acpi_checksum(rsdp, 20u) == 0u);

    /* RSDT (legacy 32-bit entries) and XSDT (64-bit) both point at the same
     * FADT/MADT; the revision-2 RSDP makes the walker follow the XSDT on a
     * 64-bit host where static objects cannot be addressed by uint32_t. */
    memcpy(rsdt, NK_ACPI_RSDT_SIG, 4u);
    put32(rsdt + 4, sizeof(rsdt));
    put32(rsdt + 36, (uint32_t)(uintptr_t)fadt);
    put32(rsdt + 40, (uint32_t)(uintptr_t)madt);
    fix_checksum(rsdt, sizeof(rsdt), 9u);

    memcpy(xsdt, NK_ACPI_XSDT_SIG, 4u);
    put32(xsdt + 4, sizeof(xsdt));
    put64(xsdt + 36, (uint64_t)(uintptr_t)fadt);
    put64(xsdt + 44, (uint64_t)(uintptr_t)madt);
    fix_checksum(xsdt, sizeof(xsdt), 9u);

    memcpy(fadt, NK_ACPI_FADT_SIG, 4u);
    put32(fadt + 4, sizeof(fadt));
    put16(fadt + 64, 0x400u);
    put16(fadt + 66, 0x404u);
    fix_checksum(fadt, sizeof(fadt), 9u);

    memcpy(madt, NK_ACPI_MADT_SIG, 4u);
    put32(madt + 4, sizeof(madt));
    put32(madt + 24, 0xfee00000u);
    put32(madt + 28, 1u);
    pos = 44u;
    madt[pos] = 0u;     /* LAPIC */
    madt[pos + 1u] = 8u;
    madt[pos + 2u] = 0u; /* processor id */
    madt[pos + 3u] = 0u; /* lapic id */
    put32(madt + pos + 4u, 1u);
    pos += 8u;
    madt[pos] = 1u;      /* IO APIC */
    madt[pos + 1u] = 12u;
    madt[pos + 2u] = 0u;
    put32(madt + pos + 4u, 0xfec00000u);
    put32(madt + pos + 8u, 0u);
    fix_checksum(madt, sizeof(madt), 9u);

    memcpy(rsdp, NK_ACPI_RSDP_SIG, 8u);
    rsdp[9] = 'T';
    rsdp[10] = 'E';
    rsdp[11] = 'S';
    rsdp[12] = 'T';
    rsdp[15] = 2u;
    put32(rsdp + 16, (uint32_t)(uintptr_t)rsdt);
    put32(rsdp + 20, 36u);
    put64(rsdp + 24, (uint64_t)(uintptr_t)xsdt);
    fix_checksum(rsdp, 20u, 8u);
    fix_checksum(rsdp, 36u, 32u);

    CHECK("acpi rsdp valid", nk_acpi_rsdp_valid(rsdp) == 1);
    rsdp[0] = 'X';
    CHECK("acpi rsdp bad sig", nk_acpi_rsdp_valid(rsdp) == 0);
    rsdp[0] = 'R';

    CHECK("acpi parse", nk_acpi_parse(rsdp, &info) == 0);
    CHECK("acpi parse fields",
          (info.present == 1u) && (info.fadt_present == 1u) &&
          (info.madt_present == 1u) && (info.pm1a_io_base == 0x400u) &&
          (info.pm1b_io_base == 0x404u));
    CHECK("acpi parse madt",
          (info.lapic_addr == 0xfee00000u) &&
          (info.ioapic_addr == 0xfec00000u) && (info.ioapic_gsi_base == 0u) &&
          (info.cpu_count == 1u) && (info.table_count == 2u));

    for (pos = 0u; pos < 20u; pos++) {
        sum += rsdp[pos];
    }
    CHECK("acpi checksums valid", (uint8_t)sum == 0u);
}

/* ------------------------------------------------------------------ */
/* Sandbox.                                                            */
/* ------------------------------------------------------------------ */

static void test_sandbox(void)
{
    static uint8_t mem[64];
    nk_sandbox_t sb;
    uint8_t tmp[16];

    CHECK("sandbox init",
          nk_sandbox_init(&sb, (uint64_t)(uintptr_t)mem, sizeof(mem), 0u) ==
              NK_STATUS_OK);
    CHECK("sandbox write",
          nk_sandbox_write(&sb, (uint64_t)(uintptr_t)mem, "AB", 2u) ==
              NK_STATUS_OK);
    CHECK("sandbox read",
          (nk_sandbox_read(&sb, (uint64_t)(uintptr_t)mem, tmp, 2u) ==
           NK_STATUS_OK) &&
          (memcmp(tmp, "AB", 2u) == 0));
    CHECK("sandbox contains",
          (nk_sandbox_contains(&sb, (uint64_t)(uintptr_t)mem, 64u) == 1) &&
          (nk_sandbox_contains(&sb, (uint64_t)(uintptr_t)mem + 60u, 8u) == 0));
    CHECK("sandbox out of range",
          nk_sandbox_read(&sb, (uint64_t)(uintptr_t)mem + 60u, tmp, 8u) !=
              NK_STATUS_OK);
    CHECK("sandbox null buffer",
          nk_sandbox_read(&sb, (uint64_t)(uintptr_t)mem, (void *)0, 1u) ==
              NK_STATUS_INVALID_ARGUMENT);
    CHECK("sandbox readonly init",
          nk_sandbox_init(&sb, (uint64_t)(uintptr_t)mem, sizeof(mem),
                          NK_SANDBOX_READONLY) == NK_STATUS_OK);
    CHECK("sandbox readonly write",
          nk_sandbox_write(&sb, (uint64_t)(uintptr_t)mem, "Z", 1u) ==
              NK_STATUS_PERMISSION_DENIED);
    CHECK("sandbox readonly read",
          nk_sandbox_read(&sb, (uint64_t)(uintptr_t)mem, tmp, 1u) ==
              NK_STATUS_OK);
}

/* ------------------------------------------------------------------ */
/* Shell.                                                              */
/* ------------------------------------------------------------------ */

static char g_shell_out[8192];
static uint32_t g_shell_out_len;
static char g_last_exec[NK_VFS_PATH_MAX];
static uint32_t g_ui_start_count;

static void shell_out_reset(void)
{
    g_shell_out_len = 0u;
    g_shell_out[0] = '\0';
}

static void shell_sink(const char *text, uint32_t len)
{
    uint32_t i;

    for (i = 0u; (i < len) && ((g_shell_out_len + 1u) < sizeof(g_shell_out));
         i++) {
        g_shell_out[g_shell_out_len++] = text[i];
    }
    g_shell_out[g_shell_out_len] = '\0';
}

static nk_status_t shell_exec(const char *path)
{
    uint32_t i = 0u;

    while ((path[i] != '\0') && (i < (sizeof(g_last_exec) - 1u))) {
        g_last_exec[i] = path[i];
        i++;
    }
    g_last_exec[i] = '\0';
    return NK_STATUS_OK;
}

static nk_status_t shell_ui_start(void)
{
    g_ui_start_count++;
    return (g_ui_start_count == 1u) ? NK_STATUS_OK
                                    : NK_STATUS_ALREADY_EXISTS;
}

static void test_shell(void)
{
    static uint8_t mem[64];
    nk_shell64_ctx_t ctx;
    char line[128];
    char addr_hex[32];

    nk_vfs_init();
    CHECK("shell vfs seed", nk_vfs_write_file("@/data/hello.txt", "HELLO",
                                              5u) == NK_STATUS_OK);
    memset(mem, 0, sizeof(mem));

    nk_shell64_init(&ctx);
    ctx.puts = shell_sink;
    ctx.readdir = nk_vfs_readdir;
    ctx.read = nk_vfs_read_file;
    ctx.exec = shell_exec;
    ctx.ui_start = shell_ui_start;
    g_ui_start_count = 0u;
    (void)nk_sandbox_init(&ctx.sandbox, (uint64_t)(uintptr_t)mem,
                          sizeof(mem), 0u);
    ctx.ticks = 123u;
    ctx.task_count = 2u;

    shell_out_reset();
    nk_shell64_prompt(&ctx);
    CHECK("shell prompt exact",
          (g_shell_out_len == 4u) && (strcmp(g_shell_out, "nk) ") == 0));

    shell_out_reset();
    nk_shell64_banner(&ctx);
    CHECK("shell banner", strstr(g_shell_out, "type help") != (char *)0);

    shell_out_reset();
    strcpy(line, "help");
    CHECK("shell help", (nk_shell64_exec(&ctx, line) == 0) &&
                           (strstr(g_shell_out, "mem rd") != (char *)0) &&
                           (strstr(g_shell_out, "ui start") != (char *)0));

    shell_out_reset();
    strcpy(line, "ls");
    CHECK("shell ls", (nk_shell64_exec(&ctx, line) == 0) &&
                          (strstr(g_shell_out, "data") != (char *)0));

    shell_out_reset();
    strcpy(line, "cat @/data/hello.txt");
    CHECK("shell cat", (nk_shell64_exec(&ctx, line) == 0) &&
                           (strstr(g_shell_out, "HELLO") != (char *)0));

    shell_out_reset();
    strcpy(line, "echo one two");
    CHECK("shell echo", (nk_shell64_exec(&ctx, line) == 0) &&
                            (strstr(g_shell_out, "one two") != (char *)0));

    nk_snprintf(addr_hex, (uint32_t)sizeof(addr_hex), "%llx",
                (unsigned long long)(uintptr_t)mem);
    shell_out_reset();
    nk_snprintf(line, (uint32_t)sizeof(line), "mem rd %s 4", addr_hex);
    CHECK("shell mem rd", (nk_shell64_exec(&ctx, line) == 0) &&
                              (strstr(g_shell_out, addr_hex) != (char *)0));

    shell_out_reset();
    nk_snprintf(line, (uint32_t)sizeof(line), "mem wr %s 4142", addr_hex);
    CHECK("shell mem wr", (nk_shell64_exec(&ctx, line) == 0) &&
                              (mem[0] == 0x41u) && (mem[1] == 0x42u));

    shell_out_reset();
    strcpy(line, "exec @/data/hello.txt");
    CHECK("shell exec", (nk_shell64_exec(&ctx, line) == 0) &&
                            (strcmp(g_last_exec, "@/data/hello.txt") == 0));

    shell_out_reset();
    strcpy(line, "ui start");
    CHECK("shell ui start", (nk_shell64_exec(&ctx, line) == 0) &&
                                (g_ui_start_count == 1u) &&
                                (strstr(g_shell_out, "InterfaceKit started") !=
                                 (char *)0));

    shell_out_reset();
    strcpy(line, "ui start");
    CHECK("shell ui start again", (nk_shell64_exec(&ctx, line) == 0) &&
                                      (g_ui_start_count == 2u) &&
                                      (strstr(g_shell_out, "already running") !=
                                       (char *)0));

    shell_out_reset();
    strcpy(line, "ui stop");
    CHECK("shell ui usage", (nk_shell64_exec(&ctx, line) == 0) &&
                                (g_ui_start_count == 2u) &&
                                (strstr(g_shell_out, "usage: ui start") !=
                                 (char *)0));

    shell_out_reset();
    strcpy(line, "bogus");
    CHECK("shell unknown",
          (nk_shell64_exec(&ctx, line) == 0) &&
          (strstr(g_shell_out, "unknown command: bogus") != (char *)0));

    strcpy(line, "exit");
    CHECK("shell exit", nk_shell64_exec(&ctx, line) == 1);
}

/* ------------------------------------------------------------------ */
/* USB-as-SATA with a RAM-disk backend.                                */
/* ------------------------------------------------------------------ */

static uint8_t g_disk[4096];

static int disk_read(void *ctx, uint32_t lun, uint64_t lba, uint32_t count,
                     void *buffer)
{
    uint64_t offset = (lba + (uint64_t)count) * 512u;

    (void)ctx;
    if ((lun != 0u) || (offset > sizeof(g_disk))) {
        return -1;
    }
    memcpy(buffer, g_disk + (uint32_t)(lba * 512u),
           (size_t)((uint64_t)count * 512u));
    return 0;
}

static int disk_write(void *ctx, uint32_t lun, uint64_t lba, uint32_t count,
                      const void *buffer)
{
    (void)ctx;
    (void)lun;
    (void)lba;
    (void)count;
    (void)buffer;
    return -1; /* not needed by these tests */
}

static int disk_info(void *ctx, uint32_t lun, nk_uas_lun_info_t *out)
{
    (void)ctx;
    if (lun != 0u) {
        return -1;
    }
    memset(out, 0, sizeof(*out));
    out->peripheral = 0u;
    out->removable = 0x80u;
    memcpy(out->vendor, "NULLK   ", 8u);
    memcpy(out->product, "RAMDISK         ", 16u);
    out->block_size = 512u;
    out->block_count = sizeof(g_disk) / 512u;
    return 0;
}

static void make_cbw(nk_uas_cbw_t *cbw, uint32_t tag, uint32_t lun,
                     const uint8_t *cdb, uint32_t cdb_len, uint32_t data_len)
{
    memset(cbw, 0, sizeof(*cbw));
    memcpy(cbw->signature, NK_UAS_SIG_CBW, 4u);
    cbw->tag = tag;
    cbw->data_len = data_len;
    cbw->flags = NK_UAS_DIR_IN;
    cbw->lun = (uint8_t)lun;
    cbw->cdb_len = (uint8_t)cdb_len;
    memcpy(cbw->cdb, cdb, cdb_len);
}

static void test_usb_as_sata(void)
{
    nk_uas_bridge_t bridge;
    nk_uas_cbw_t cbw;
    nk_uas_csw_t csw;
    nk_usbprog_header_t hdr;
    uint8_t cdb[16];
    uint8_t out[512];
    uint8_t capacity[8];
    uint32_t i;
    int status;

    for (i = 0u; i < sizeof(g_disk); i++) {
        g_disk[i] = (uint8_t)(i ^ 0xa5u);
    }
    nk_uas_bridge_init(&bridge, (void *)0, disk_read, disk_write, disk_info);

    memset(cdb, 0, sizeof(cdb));
    cdb[0] = NK_SCSI_READ_10;
    make_cbw(&cbw, 1u, 0u, cdb, 10u, 512u);
    CHECK("uas cbw valid", nk_uas_cbw_valid(&cbw) == 1);
    cbw.data_len = 513u;
    CHECK("uas cbw unaligned", nk_uas_cbw_valid(&cbw) == 0);
    cbw.data_len = 512u;
    cbw.cdb_len = 9u;
    CHECK("uas cbw short cdb", nk_uas_cbw_valid(&cbw) == 0);
    cbw.cdb_len = 10u;
    cbw.lun = NK_UAS_MAX_LUN;
    CHECK("uas cbw bad lun", nk_uas_cbw_valid(&cbw) == 0);
    cbw.lun = 0u;
    cbw.signature[0] = 'X';
    CHECK("uas cbw bad sig", nk_uas_cbw_valid(&cbw) == 0);

    memset(cdb, 0, sizeof(cdb));
    cdb[0] = NK_SCSI_INQUIRY;
    make_cbw(&cbw, 0x1234u, 0u, cdb, 6u, 36u);
    memset(out, 0, sizeof(out));
    memset(&csw, 0xcc, sizeof(csw));
    status = nk_uas_exec(&bridge, &cbw, out, sizeof(out), &csw);
    CHECK("uas inquiry exec", status == 0);
    CHECK("uas inquiry data",
          (out[0] == 0x00u) && (out[1] == 0x80u) && (out[2] == 0x01u) &&
          (out[3] == 0x00u) && (out[4] == 31u) &&
          (memcmp(out + 8, "NULLK   ", 8u) == 0));
    CHECK("uas csw inquiry",
          (memcmp(csw.signature, NK_UAS_SIG_CSW, 4u) == 0) &&
          (csw.tag == 0x1234u) && (csw.residue == 0u) &&
          (csw.status == NK_BOT_STATUS_OK));

    memset(cdb, 0, sizeof(cdb));
    cdb[0] = NK_SCSI_READ_CAPACITY_10;
    make_cbw(&cbw, 0x1235u, 0u, cdb, 10u, 8u);
    status = nk_uas_exec(&bridge, &cbw, capacity, sizeof(capacity), &csw);
    CHECK("uas read capacity exec", status == 0);
    CHECK("uas read capacity data",
          (capacity[0] == 0u) && (capacity[1] == 0u) &&
          (capacity[2] == 0u) && (capacity[3] == 7u) &&
          (capacity[4] == 0u) && (capacity[5] == 0u) &&
          (capacity[6] == 2u) && (capacity[7] == 0u));
    CHECK("uas csw capacity",
          (csw.tag == 0x1235u) && (csw.residue == 0u) &&
          (csw.status == NK_BOT_STATUS_OK));

    memset(cdb, 0, sizeof(cdb));
    cdb[0] = NK_SCSI_READ_10;
    cdb[5] = 1u; /* LBA 1 */
    cdb[8] = 1u; /* one block */
    make_cbw(&cbw, 0x1236u, 0u, cdb, 10u, 512u);
    memset(out, 0, sizeof(out));
    status = nk_uas_exec(&bridge, &cbw, out, sizeof(out), &csw);
    CHECK("uas read10 exec", status == 0);
    CHECK("uas read10 pattern",
          (memcmp(out, g_disk + 512u, 512u) == 0) &&
          (out[0] == (uint8_t)(512u ^ 0xa5u)));
    CHECK("uas csw read10",
          (csw.tag == 0x1236u) && (csw.residue == 0u) &&
          (csw.status == NK_BOT_STATUS_OK));
    CHECK("uas bridge tag", bridge.tag == 0x1236u);

    memset(&hdr, 0, sizeof(hdr));
    memcpy(hdr.magic, NK_USBPROG_MAGIC, 8u);
    hdr.version = 1u;
    hdr.elf_size = 1024u;
    hdr.elf_lba = 2u;
    CHECK("usbprog valid", nk_usbprog_valid(&hdr) == 1);
    hdr.version = 2u;
    CHECK("usbprog bad version", nk_usbprog_valid(&hdr) == 0);
    hdr.version = 1u;
    hdr.elf_lba = 1u;
    CHECK("usbprog bad lba", nk_usbprog_valid(&hdr) == 0);
}

int main(void)
{
    test_printk();
    test_vfs();
    test_vfs_tar();
    test_elf64();
    test_acpi();
    test_sandbox();
    test_shell();
    test_usb_as_sata();

    printf("amd64 host tests: %d/%d passed\n", g_passed, g_checks);
    return (g_passed == g_checks) ? 0 : 1;
}
