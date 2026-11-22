/*
 * null_kernel nk) shell command engine.
 *
 * Shared by the kernel fallback shell and the userspace shell; only the
 * callbacks differ. The parser tokenises the supplied (mutable) line in place
 * and never writes outside it or the fixed scratch buffers below.
 */
#include "nullk/shell64.h"
#include "nullk/printk.h"
#include "nullk/usb_as_sata.h"

#include <stdarg.h>
#include <stddef.h>

#define NK_SH_ARGV_MAX      16u
#define NK_SH_DIRENT_MAX    32u
#define NK_SH_CAT_CHUNK     256u
#define NK_SH_MEM_MAX       256u
#define NK_SH_USB_CHUNK_SECTORS 8u

/* ------------------------------------------------------------------ */
/* Local helpers.                                                      */
/* ------------------------------------------------------------------ */

static void nk_sh_memset(void *dst, int value, uint32_t n)
{
    uint8_t *p = (uint8_t *)dst;
    uint32_t i;

    for (i = 0u; i < n; i++) {
        p[i] = (uint8_t)value;
    }
}

static void nk_sh_memcpy(void *dst, const void *src, uint32_t n)
{
    uint8_t *d = (uint8_t *)dst;
    const uint8_t *s = (const uint8_t *)src;
    uint32_t i;

    for (i = 0u; i < n; i++) {
        d[i] = s[i];
    }
}

static uint32_t nk_sh_strlen(const char *s)
{
    uint32_t n = 0u;

    while ((s != (const char *)0) && (s[n] != '\0')) {
        n++;
    }
    return n;
}

static int nk_sh_streq(const char *a, const char *b)
{
    while ((*a != '\0') && (*a == *b)) {
        a++;
        b++;
    }
    return (*a == *b) ? 1 : 0;
}

static void nk_sh_emit(nk_shell64_ctx_t *ctx, const char *text, uint32_t len)
{
    uint32_t i;

    if ((ctx == (nk_shell64_ctx_t *)0) || (text == (const char *)0)) {
        return;
    }
    if (ctx->puts != (void (*)(const char *, uint32_t))0) {
        ctx->puts(text, len);
        return;
    }
    if (ctx->putc != (void (*)(char))0) {
        for (i = 0u; i < len; i++) {
            ctx->putc(text[i]);
        }
    }
}

static void nk_sh_puts(nk_shell64_ctx_t *ctx, const char *text)
{
    nk_sh_emit(ctx, text, nk_sh_strlen(text));
}

static void nk_sh_printf(nk_shell64_ctx_t *ctx, const char *fmt, ...)
{
    char buf[192];
    va_list ap;
    int n;

    va_start(ap, fmt);
    n = nk_vsnprintf(buf, (uint32_t)sizeof(buf), fmt, ap);
    va_end(ap);
    if (n <= 0) {
        return;
    }
    if ((uint32_t)n > (sizeof(buf) - 1u)) {
        n = (int)(sizeof(buf) - 1u);
    }
    nk_sh_emit(ctx, buf, (uint32_t)n);
}

/* Accept an optional 0x prefix, leading zeros, up to 64 bits. */
static int nk_sh_parse_hex(const char *s, uint64_t *out)
{
    uint64_t value = 0u;
    uint32_t digits = 0u;

    if ((s == (const char *)0) || (out == (uint64_t *)0)) {
        return -1;
    }
    if ((s[0] == '0') && ((s[1] == 'x') || (s[1] == 'X'))) {
        s += 2;
    }
    if (*s == '\0') {
        return -1;
    }
    while (*s != '\0') {
        uint32_t digit;

        if ((*s >= '0') && (*s <= '9')) {
            digit = (uint32_t)(*s - '0');
        } else if ((*s >= 'a') && (*s <= 'f')) {
            digit = (uint32_t)(*s - 'a') + 10u;
        } else if ((*s >= 'A') && (*s <= 'F')) {
            digit = (uint32_t)(*s - 'A') + 10u;
        } else {
            return -1;
        }
        if (value > ((UINT64_MAX - (uint64_t)digit) / 16u)) {
            return -1; /* overflow */
        }
        value = (value * 16u) + (uint64_t)digit;
        digits++;
        s++;
    }
    if (digits == 0u) {
        return -1;
    }
    *out = value;
    return 0;
}

static int nk_sh_hex_nibble(char c, uint32_t *out)
{
    if ((c >= '0') && (c <= '9')) {
        *out = (uint32_t)(c - '0');
        return 0;
    }
    if ((c >= 'a') && (c <= 'f')) {
        *out = (uint32_t)(c - 'a') + 10u;
        return 0;
    }
    if ((c >= 'A') && (c <= 'F')) {
        *out = (uint32_t)(c - 'A') + 10u;
        return 0;
    }
    return -1;
}

/* Parse a possibly space-separated hex byte string into `out`. */
static int nk_sh_parse_hex_bytes(const char *s, uint8_t *out, uint32_t cap,
                                 uint32_t *count)
{
    uint32_t n = 0u;
    int have_hi = 0;
    uint32_t hi = 0u;

    while (*s != '\0') {
        uint32_t digit;

        if ((*s == ' ') || (*s == '\t')) {
            s++;
            continue;
        }
        if (nk_sh_hex_nibble(*s, &digit) != 0) {
            return -1;
        }
        s++;
        if (!have_hi) {
            hi = digit;
            have_hi = 1;
        } else {
            if (n >= cap) {
                return -1;
            }
            out[n++] = (uint8_t)((hi << 4) | digit);
            have_hi = 0;
        }
    }
    if (have_hi) {
        return -1; /* odd number of nibbles */
    }
    *count = n;
    return 0;
}

static uint32_t nk_sh_tokenize(char *line, char **argv, uint32_t max)
{
    uint32_t argc = 0u;
    char *p = line;

    while (*p != '\0') {
        while ((*p == ' ') || (*p == '\t')) {
            p++;
        }
        if ((*p == '\0') || (*p == '#')) {
            break;
        }
        if (argc >= max) {
            break;
        }
        argv[argc++] = p;
        while ((*p != '\0') && (*p != ' ') && (*p != '\t') && (*p != '#')) {
            p++;
        }
        if (*p == '#') {
            *p = '\0';
            break;
        }
        if (*p != '\0') {
            *p++ = '\0';
        }
    }
    return argc;
}

/* ------------------------------------------------------------------ */
/* Commands.                                                           */
/* ------------------------------------------------------------------ */

void nk_shell64_banner(nk_shell64_ctx_t *ctx)
{
    nk_sh_puts(ctx, "+------------------------------+\n");
    nk_sh_puts(ctx, "|  null_kernel  nk) shell      |\n");
    nk_sh_puts(ctx, "+------------------------------+\n");
    nk_sh_puts(ctx, "null_kernel nk) shell - type help\n");
}

void nk_shell64_help(nk_shell64_ctx_t *ctx)
{
    nk_sh_puts(ctx,
               "help\n"
               "info\n"
               "pwd\n"
               "ls [path]\n"
               "cat <path>\n"
               "echo <args>\n"
               "mem rd <hex> [len]\n"
               "mem wr <hex> <hexbytes>\n"
               "exec <path>\n"
               "ui start\n"
               "usb lun <n>\n"
               "usb load <n>\n"
               "clear\n"
               "exit | quit\n");
}

void nk_shell64_init(nk_shell64_ctx_t *ctx)
{
    if (ctx == (nk_shell64_ctx_t *)0) {
        return;
    }
    nk_sh_memset(ctx, 0, (uint32_t)sizeof(*ctx));
}

void nk_shell64_prompt(nk_shell64_ctx_t *ctx)
{
    nk_sh_emit(ctx, "nk) ", 4u);
}

static void nk_sh_cmd_info(nk_shell64_ctx_t *ctx)
{
    nk_sh_printf(ctx, "ABI v3 | ticks: %llu | tasks: %u | sandbox: 0x%llx + 0x%llx (flags 0x%x)\n",
                 (unsigned long long)ctx->ticks, ctx->task_count,
                 (unsigned long long)ctx->sandbox.base,
                 (unsigned long long)ctx->sandbox.size, ctx->flags);
}

static void nk_sh_cmd_ls(nk_shell64_ctx_t *ctx, const char *path)
{
    nk_vfs_dirent_t entries[NK_SH_DIRENT_MAX];
    uint32_t count = 0u;
    nk_status_t status;

    if (ctx->readdir == (nk_status_t (*)(const char *, nk_vfs_dirent_t *,
                                         uint32_t, uint32_t *))0) {
        nk_sh_puts(ctx, "unavailable\n");
        return;
    }
    status = ctx->readdir((path != (const char *)0) ? path : "@/",
                          entries, NK_SH_DIRENT_MAX, &count);
    if (status != NK_STATUS_OK) {
        nk_sh_printf(ctx, "ls: failed (%u)\n", status);
        return;
    }
    {
        uint32_t i;

        for (i = 0u; i < count; i++) {
            char type = (entries[i].type == NK_VFS_DIR) ? 'd' : 'f';

            nk_sh_printf(ctx, "%c %s %u\n", type, entries[i].name,
                         entries[i].size);
        }
    }
}

static void nk_sh_cmd_cat(nk_shell64_ctx_t *ctx, const char *path)
{
    uint8_t buf[NK_SH_CAT_CHUNK];
    uint32_t offset = 0u;

    if (ctx->read == (nk_status_t (*)(const char *, uint32_t, void *,
                                      uint32_t, uint32_t *))0) {
        nk_sh_puts(ctx, "unavailable\n");
        return;
    }
    for (;;) {
        uint32_t len = 0u;
        nk_status_t status = ctx->read(path, offset, buf,
                                       (uint32_t)sizeof(buf), &len);

        if (status != NK_STATUS_OK) {
            nk_sh_printf(ctx, "cat: failed (%u)\n", status);
            return;
        }
        if (len == 0u) {
            break;
        }
        nk_sh_emit(ctx, (const char *)buf, len);
        offset += len;
        if (len < (uint32_t)sizeof(buf)) {
            break;
        }
    }
}

static void nk_sh_cmd_mem_rd(nk_shell64_ctx_t *ctx, uint32_t argc,
                             char **argv)
{
    uint8_t buf[NK_SH_MEM_MAX];
    uint64_t addr;
    uint64_t requested = 16u;
    uint32_t len;
    uint32_t i;
    nk_status_t status;

    if (argc < 3u) {
        nk_sh_puts(ctx, "usage: mem rd <hex> [len]\n");
        return;
    }
    if (nk_sh_parse_hex(argv[2], &addr) != 0) {
        nk_sh_puts(ctx, "mem rd: bad address\n");
        return;
    }
    if (argc >= 4u) {
        if (nk_sh_parse_hex(argv[3], &requested) != 0) {
            nk_sh_puts(ctx, "mem rd: bad length\n");
            return;
        }
    }
    if ((requested == 0u) || (requested > NK_SH_MEM_MAX)) {
        nk_sh_puts(ctx, "mem rd: length out of range\n");
        return;
    }
    len = (uint32_t)requested;
    status = nk_sandbox_read(&ctx->sandbox, addr, buf, len);
    if (status != NK_STATUS_OK) {
        nk_sh_printf(ctx, "mem rd: failed (%u)\n", status);
        return;
    }
    for (i = 0u; i < len; i += 16u) {
        char line[80];
        uint32_t j;
        uint32_t n;

        nk_snprintf(line, (uint32_t)sizeof(line), "%08llx: ",
                    (unsigned long long)(addr + (uint64_t)i));
        nk_sh_puts(ctx, line);
        n = ((len - i) < 16u) ? (len - i) : 16u;
        for (j = 0u; j < n; j++) {
            nk_snprintf(line, (uint32_t)sizeof(line), "%02x ", buf[i + j]);
            nk_sh_puts(ctx, line);
        }
        nk_sh_puts(ctx, "\n");
    }
}

static void nk_sh_cmd_mem_wr(nk_shell64_ctx_t *ctx, uint32_t argc,
                             char **argv)
{
    uint8_t buf[NK_SH_MEM_MAX];
    uint32_t count = 0u;
    uint64_t addr;
    nk_status_t status;

    if (argc < 4u) {
        nk_sh_puts(ctx, "usage: mem wr <hex> <hexbytes>\n");
        return;
    }
    if (nk_sh_parse_hex(argv[2], &addr) != 0) {
        nk_sh_puts(ctx, "mem wr: bad address\n");
        return;
    }
    if (nk_sh_parse_hex_bytes(argv[3], buf, NK_SH_MEM_MAX, &count) != 0) {
        nk_sh_puts(ctx, "mem wr: bad hex data\n");
        return;
    }
    status = nk_sandbox_write(&ctx->sandbox, addr, buf, count);
    if (status != NK_STATUS_OK) {
        nk_sh_printf(ctx, "mem wr: failed (%u)\n", status);
        return;
    }
    nk_sh_printf(ctx, "mem wr: %u byte(s)\n", count);
}

static void nk_sh_cmd_exec(nk_shell64_ctx_t *ctx, const char *path)
{
    nk_status_t status;

    if (ctx->exec == (nk_status_t (*)(const char *))0) {
        nk_sh_puts(ctx, "unavailable\n");
        return;
    }
    status = ctx->exec(path);
    nk_sh_printf(ctx, "exec %s: %u\n", path, status);
}

static void nk_sh_cmd_ui(nk_shell64_ctx_t *ctx, uint32_t argc, char **argv)
{
    nk_status_t status;

    if (argc < 2u || !nk_sh_streq(argv[1], "start")) {
        nk_sh_puts(ctx, "usage: ui start\n");
        return;
    }
    if (ctx->ui_start == (nk_status_t (*)(void))0) {
        nk_sh_puts(ctx, "ui: unavailable in this shell\n");
        return;
    }
    status = ctx->ui_start();
    if (status == NK_STATUS_OK) {
        nk_sh_puts(ctx, "ui: InterfaceKit started\n");
    } else if (status == NK_STATUS_ALREADY_EXISTS) {
        nk_sh_puts(ctx, "ui: InterfaceKit is already running\n");
    } else {
        nk_sh_printf(ctx, "ui start: %u\n", (uint32_t)status);
    }
}

static void nk_sh_cmd_usb(nk_shell64_ctx_t *ctx, uint32_t argc, char **argv)
{
    uint8_t sector[512];
    uint8_t payload[512u * NK_SH_USB_CHUNK_SECTORS];
    nk_usbprog_header_t header;
    uint64_t hex_lun;
    uint64_t sectors;
    uint64_t lba;
    uint32_t lun;
    uint32_t remaining;
    nk_status_t status;

    if (argc < 3u) {
        nk_sh_puts(ctx, "usage: usb lun <n> | usb load <n>\n");
        return;
    }
    if (nk_sh_parse_hex(argv[2], &hex_lun) != 0) {
        nk_sh_puts(ctx, "usb: bad LUN\n");
        return;
    }
    if (hex_lun >= NK_UAS_MAX_LUN) {
        nk_sh_puts(ctx, "usb: LUN out of range\n");
        return;
    }
    lun = (uint32_t)hex_lun;
    if (ctx->usb_read == (nk_status_t (*)(uint32_t, uint64_t, uint32_t,
                                          void *))0) {
        nk_sh_puts(ctx, "unavailable\n");
        return;
    }
    status = ctx->usb_read(lun, NK_USBPROG_SECTOR, 1u, sector);
    if (status != NK_STATUS_OK) {
        nk_sh_printf(ctx, "usb: header read failed (%u)\n", status);
        return;
    }
    nk_sh_memcpy(&header, sector, sizeof(header));
    if (nk_usbprog_valid(&header) == 0) {
        nk_sh_puts(ctx, "usb: no valid NKUSBPRG header\n");
        return;
    }

    if (nk_sh_streq(argv[1], "lun")) {
        nk_sh_printf(ctx, "usb lun %u: elf_size=%u elf_lba=%llu\n", lun,
                     header.elf_size, (unsigned long long)header.elf_lba);
        return;
    }

    /* 'load': read the ELF payload through the callback as well. */
    sectors = (((uint64_t)header.elf_size + 511u) / 512u);
    lba = header.elf_lba;
    remaining = (uint32_t)sectors;
    while (remaining != 0u) {
        uint32_t chunk = (remaining > NK_SH_USB_CHUNK_SECTORS)
                             ? NK_SH_USB_CHUNK_SECTORS
                             : remaining;

        status = ctx->usb_read(lun, lba, chunk, payload);
        if (status != NK_STATUS_OK) {
            nk_sh_printf(ctx, "usb: payload read failed (%u)\n", status);
            return;
        }
        lba += chunk;
        remaining -= chunk;
    }

    /* The shell has no filesystem-write callback. The caller is responsible
     * for installing the downloaded image at this path before 'usb load'. */
    nk_sh_puts(ctx, "usb load: caller must install the image at "
                    "@/data/user/download.elf; invoking exec\n");
    if (ctx->exec == (nk_status_t (*)(const char *))0) {
        nk_sh_puts(ctx, "unavailable\n");
        return;
    }
    status = ctx->exec("@/data/user/download.elf");
    nk_sh_printf(ctx, "usb load: exec status %u\n", status);
}

int nk_shell64_exec(nk_shell64_ctx_t *ctx, char *line)
{
    char *argv[NK_SH_ARGV_MAX];
    uint32_t argc;

    if ((ctx == (nk_shell64_ctx_t *)0) || (line == (char *)0)) {
        return 0;
    }
    argc = nk_sh_tokenize(line, argv, NK_SH_ARGV_MAX);
    if (argc == 0u) {
        return 0;
    }

    if (nk_sh_streq(argv[0], "help")) {
        nk_shell64_help(ctx);
    } else if (nk_sh_streq(argv[0], "info")) {
        nk_sh_cmd_info(ctx);
    } else if (nk_sh_streq(argv[0], "pwd")) {
        nk_sh_puts(ctx, "@/");
    } else if (nk_sh_streq(argv[0], "ls")) {
        nk_sh_cmd_ls(ctx, (argc > 1u) ? argv[1] : (const char *)0);
    } else if (nk_sh_streq(argv[0], "cat")) {
        if (argc < 2u) {
            nk_sh_puts(ctx, "usage: cat <path>\n");
        } else {
            nk_sh_cmd_cat(ctx, argv[1]);
        }
    } else if (nk_sh_streq(argv[0], "echo")) {
        uint32_t i;

        for (i = 1u; i < argc; i++) {
            if (i > 1u) {
                nk_sh_puts(ctx, " ");
            }
            nk_sh_puts(ctx, argv[i]);
        }
        nk_sh_puts(ctx, "\n");
    } else if (nk_sh_streq(argv[0], "mem")) {
        if (argc < 2u) {
            nk_sh_puts(ctx, "usage: mem rd|wr ...\n");
        } else if (nk_sh_streq(argv[1], "rd")) {
            nk_sh_cmd_mem_rd(ctx, argc, argv);
        } else if (nk_sh_streq(argv[1], "wr")) {
            nk_sh_cmd_mem_wr(ctx, argc, argv);
        } else {
            nk_sh_printf(ctx, "unknown command: mem %s\n", argv[1]);
        }
    } else if (nk_sh_streq(argv[0], "exec")) {
        if (argc < 2u) {
            nk_sh_puts(ctx, "usage: exec <path>\n");
        } else {
            nk_sh_cmd_exec(ctx, argv[1]);
        }
    } else if (nk_sh_streq(argv[0], "ui")) {
        nk_sh_cmd_ui(ctx, argc, argv);
    } else if (nk_sh_streq(argv[0], "usb")) {
        nk_sh_cmd_usb(ctx, argc, argv);
    } else if (nk_sh_streq(argv[0], "clear")) {
        nk_sh_puts(ctx, "\n\n\n\n\n\n\n\n");
    } else if (nk_sh_streq(argv[0], "exit") || nk_sh_streq(argv[0], "quit")) {
        return 1;
    } else {
        nk_sh_printf(ctx, "unknown command: %s\n", argv[0]);
    }
    return 0;
}
