/*
 * null_kernel nk) shell command engine.
 *
 * Shared by the kernel fallback shell and the userspace shell; only the
 * callbacks differ. The parser tokenises the supplied (mutable) line in place
 * and never writes outside it or the fixed scratch buffers below.
 *
 * Paths are relative to ctx->cwd, which starts at the "@" root and only changes
 * through `cd`. Every filesystem command resolves through nk_shell64_resolve(),
 * so "." and ".." are handled once instead of in each command.
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
#define NK_SH_LINE_BUF      192u

static const char *const g_commands[] = {
    "help", "info", "sysinfo", "pwd", "cd", "ls", "cat", "stat", "head",
    "wc", "grep", "echo", "mem", "exec", "ui", "usb", "history",
    "clear", "exit", "quit", (const char *)0
};

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

static int nk_sh_strlt(const char *a, const char *b)
{
    while ((*a != '\0') && (*a == *b)) {
        a++;
        b++;
    }
    return (*a < *b) ? 1 : 0;
}

/* Small substring test, used by grep. */
static int nk_sh_contains(const char *hay, const char *needle)
{
    uint32_t nlen;

    if (needle == (const char *)0 || *needle == '\0') {
        return 1;
    }
    nlen = nk_sh_strlen(needle);
    while (*hay != '\0') {
        uint32_t i = 0u;

        while (i < nlen && hay[i] != '\0' && hay[i] == needle[i]) {
            i++;
        }
        if (i == nlen) {
            return 1;
        }
        hay++;
    }
    return 0;
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

static void nk_sh_putc(nk_shell64_ctx_t *ctx, char c)
{
    nk_sh_emit(ctx, &c, 1u);
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

/* Plain decimal, used by head and history. */
static int nk_sh_parse_dec(const char *s, uint32_t *out)
{
    uint32_t value = 0u;
    uint32_t digits = 0u;

    if ((s == (const char *)0) || (out == (uint32_t *)0)) {
        return -1;
    }
    while (*s != '\0') {
        if (*s < '0' || *s > '9') {
            return -1;
        }
        if (value > (UINT32_MAX - (uint32_t)(*s - '0')) / 10u) {
            return -1;
        }
        value = value * 10u + (uint32_t)(*s - '0');
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

/* Normalise any '@'/absolute/relative path into "@" or "@/a/b". */
static nk_status_t nk_sh_normalize(const char *path, char *out, uint32_t cap)
{
    const char *p = path;
    uint32_t len = 1u;

    if (out == (char *)0 || cap < 4u) {
        return NK_STATUS_INVALID_ARGUMENT;
    }
    out[0] = '@';
    out[1] = '\0';
    if (p == (const char *)0) {
        return NK_STATUS_OK;
    }
    if (*p == '@') {
        p++;
    }
    while (*p == '/') {
        p++;
    }
    while (*p != '\0') {
        const char *start = p;
        uint32_t n;

        while (*p != '\0' && *p != '/') {
            p++;
        }
        n = (uint32_t)(p - start);
        while (*p == '/') {
            p++;
        }
        if (n == 0u || (n == 1u && start[0] == '.')) {
            continue;
        }
        if (n == 2u && start[0] == '.' && start[1] == '.') {
            while (len > 1u && out[len - 1u] != '/') {
                len--;
            }
            if (len > 1u) {
                len--;
            }
            out[len] = '\0';
            continue;
        }
        if (len + n + 2u > cap) {
            return NK_STATUS_INVALID_ARGUMENT;
        }
        out[len++] = '/';
        nk_sh_memcpy(out + len, start, n);
        len += n;
        out[len] = '\0';
    }
    return NK_STATUS_OK;
}

nk_status_t nk_shell64_resolve(const nk_shell64_ctx_t *ctx, const char *arg,
                               char *out, uint32_t cap)
{
    char combined[NK_VFS_PATH_MAX * 2u];
    const char *base = (ctx != (const nk_shell64_ctx_t *)0) ? ctx->cwd : "@";
    uint32_t pos = 0u;
    const char *p;

    if (ctx == (const nk_shell64_ctx_t *)0 || out == (char *)0) {
        return NK_STATUS_INVALID_ARGUMENT;
    }
    if (arg == (const char *)0 || *arg == '\0') {
        return nk_sh_normalize(base, out, cap);
    }
    if (*arg == '@' || *arg == '/') {
        return nk_sh_normalize(arg, out, cap);
    }
    /* relative: cwd + "/" + arg */
    for (p = base; *p != '\0' && pos + 1u < sizeof(combined); p++) {
        combined[pos++] = *p;
    }
    if (pos + 1u < sizeof(combined)) {
        combined[pos++] = '/';
    }
    for (p = arg; *p != '\0' && pos + 1u < sizeof(combined); p++) {
        combined[pos++] = *p;
    }
    combined[pos] = '\0';
    return nk_sh_normalize(combined, out, cap);
}

const char *nk_shell64_cwd(const nk_shell64_ctx_t *ctx)
{
    if (ctx == (const nk_shell64_ctx_t *)0 || ctx->cwd[0] == '\0') {
        return "@";
    }
    return ctx->cwd;
}

const char *nk_shell64_command_name(uint32_t index)
{
    uint32_t i = 0u;

    while (g_commands[i] != (const char *)0) {
        if (i == index) {
            return g_commands[i];
        }
        i++;
    }
    return (const char *)0;
}

/* ------------------------------------------------------------------ */
/* History.                                                            */
/* ------------------------------------------------------------------ */

void nk_shell64_history_push(nk_shell64_ctx_t *ctx, const char *line)
{
    uint32_t slot;
    uint32_t len;
    const char *newest;

    if (ctx == (nk_shell64_ctx_t *)0 || line == (const char *)0 ||
        *line == '\0') {
        return;
    }
    newest = nk_shell64_history_at(ctx, 0u);
    if (newest != (const char *)0 && nk_sh_streq(newest, line)) {
        return; /* do not stack identical consecutive lines */
    }
    slot = ctx->history_next;
    len = nk_sh_strlen(line);
    if (len >= NK_SHELL_LINE_MAX) {
        len = NK_SHELL_LINE_MAX - 1u;
    }
    nk_sh_memcpy(ctx->history[slot], line, len);
    ctx->history[slot][len] = '\0';
    ctx->history_next = (slot + 1u) % NK_SHELL_HISTORY_MAX;
    if (ctx->history_count < NK_SHELL_HISTORY_MAX) {
        ctx->history_count++;
    }
}

const char *nk_shell64_history_at(const nk_shell64_ctx_t *ctx,
                                  uint32_t index_from_newest)
{
    uint32_t slot;

    if (ctx == (const nk_shell64_ctx_t *)0 ||
        index_from_newest >= ctx->history_count) {
        return (const char *)0;
    }
    slot = (ctx->history_next + NK_SHELL_HISTORY_MAX - 1u -
            index_from_newest) %
           NK_SHELL_HISTORY_MAX;
    return ctx->history[slot];
}

uint32_t nk_shell64_history_count(const nk_shell64_ctx_t *ctx)
{
    return (ctx != (const nk_shell64_ctx_t *)0) ? ctx->history_count : 0u;
}

/* ------------------------------------------------------------------ */
/* Commands.                                                           */
/* ------------------------------------------------------------------ */

void nk_shell64_banner(nk_shell64_ctx_t *ctx)
{
    nk_sh_puts(ctx, "Welcome to null kernel! :3\n");
}

void nk_shell64_help(nk_shell64_ctx_t *ctx)
{
    nk_sh_puts(ctx,
               "filesystem\n"
               "  pwd                     current directory\n"
               "  cd [path]               change directory (.. allowed)\n"
               "  ls [path]               list a directory\n"
               "  cat <path>              print a file\n"
               "  head <path> [lines]     first lines of a file (default 10)\n"
               "  wc <path>               lines, words and bytes\n"
               "  grep <pattern> <path>   lines containing pattern\n"
               "  stat <path>             type, size and inode\n"
               "\n"
               "system\n"
               "  help                    this list\n"
               "  info                    ABI, ticks, tasks, sandbox\n"
               "  sysinfo                 info plus uptime and cwd\n"
               "  history [n]             recent commands\n"
               "  clear                   clear the console\n"
               "  echo <args>             print arguments\n"
               "\n"
               "control\n"
               "  mem rd <hex> [len]      read the sandbox window\n"
               "  mem wr <hex> <hexbytes> write the sandbox window\n"
               "  exec <path>             SecureKernel-checked ELF exec\n"
               "  ui start                start gothicOS InterfaceKit\n"
               "  ui lvgl                 start the LVGL desktop (claims the fb)\n"
               "  usb lun <n>             inspect a NKUSBPRG LUN\n"
               "  usb load <n>            load and exec a NKUSBPRG payload\n"
               "  exit | quit             stop the shell\n");
}

void nk_shell64_init(nk_shell64_ctx_t *ctx)
{
    if (ctx == (nk_shell64_ctx_t *)0) {
        return;
    }
    nk_sh_memset(ctx, 0, (uint32_t)sizeof(*ctx));
    ctx->cwd[0] = '@';
    ctx->cwd[1] = '\0';
}

void nk_shell64_prompt(nk_shell64_ctx_t *ctx)
{
    nk_sh_emit(ctx, "@> ", 4u);
}

static void nk_sh_cmd_info(nk_shell64_ctx_t *ctx)
{
    nk_sh_printf(ctx, "ABI v3 | ticks: %llu | tasks: %u | sandbox: 0x%llx + 0x%llx (flags 0x%x)\n",
                 (unsigned long long)ctx->ticks, ctx->task_count,
                 (unsigned long long)ctx->sandbox.base,
                 (unsigned long long)ctx->sandbox.size, ctx->flags);
}

static void nk_sh_cmd_uptime(nk_shell64_ctx_t *ctx, int with_ticks)
{
    uint64_t seconds = ctx->ticks / 100u;
    unsigned hours = (unsigned)(seconds / 3600u);
    unsigned minutes = (unsigned)((seconds % 3600u) / 60u);
    unsigned secs = (unsigned)(seconds % 60u);

    if (with_ticks != 0) {
        nk_sh_printf(ctx, "up %u:%02u:%02u (%llu ticks)\n", hours, minutes,
                     secs, (unsigned long long)ctx->ticks);
    } else {
        nk_sh_printf(ctx, "up %u:%02u:%02u\n", hours, minutes, secs);
    }
}

static void nk_sh_cmd_sysinfo(nk_shell64_ctx_t *ctx)
{
    nk_sh_puts(ctx, "null_kernel / native Linux port\n");
    nk_sh_cmd_info(ctx);
    nk_sh_cmd_uptime(ctx, 1);
    nk_sh_printf(ctx, "cwd: %s\n", nk_shell64_cwd(ctx));
    nk_sh_printf(ctx, "history: %u entr%s\n", nk_shell64_history_count(ctx),
                 (nk_shell64_history_count(ctx) == 1u) ? "y" : "ies");
}

static void nk_sh_cmd_pwd(nk_shell64_ctx_t *ctx)
{
    const char *cwd = nk_shell64_cwd(ctx);

    if (nk_sh_streq(cwd, "@")) {
        nk_sh_puts(ctx, "@/\n");
        return;
    }
    nk_sh_printf(ctx, "%s/\n", cwd);
}

static void nk_sh_cmd_cd(nk_shell64_ctx_t *ctx, const char *path)
{
    char resolved[NK_VFS_PATH_MAX];
    nk_vfs_stat_t st;

    if (ctx->stat == (nk_status_t (*)(const char *, nk_vfs_stat_t *))0) {
        nk_sh_puts(ctx, "unavailable\n");
        return;
    }
    if (nk_shell64_resolve(ctx, path, resolved, (uint32_t)sizeof(resolved)) !=
        NK_STATUS_OK) {
        nk_sh_puts(ctx, "cd: bad path\n");
        return;
    }
    if (ctx->stat(resolved, &st) != NK_STATUS_OK) {
        nk_sh_printf(ctx, "cd: %s: not found\n", resolved);
        return;
    }
    if (st.type != NK_VFS_DIR) {
        nk_sh_printf(ctx, "cd: %s: not a directory\n", resolved);
        return;
    }
    nk_sh_memcpy(ctx->cwd, resolved, nk_sh_strlen(resolved) + 1u);
}

static void nk_sh_cmd_ls(nk_shell64_ctx_t *ctx, const char *path)
{
    char resolved[NK_VFS_PATH_MAX];
    nk_vfs_dirent_t entries[NK_SH_DIRENT_MAX];
    uint32_t count = 0u;
    nk_status_t status;
    uint32_t i;
    uint32_t j;

    if (ctx->readdir == (nk_status_t (*)(const char *, nk_vfs_dirent_t *,
                                         uint32_t, uint32_t *))0) {
        nk_sh_puts(ctx, "unavailable\n");
        return;
    }
    if (nk_shell64_resolve(ctx, path, resolved, (uint32_t)sizeof(resolved)) !=
        NK_STATUS_OK) {
        nk_sh_puts(ctx, "ls: bad path\n");
        return;
    }
    status = ctx->readdir(resolved, entries, NK_SH_DIRENT_MAX, &count);
    if (status != NK_STATUS_OK) {
        nk_sh_printf(ctx, "ls: %s: failed (%u)\n", resolved, status);
        return;
    }
    /* Directories first, then names, so listings are stable. */
    for (i = 0u; i + 1u < count; i++) {
        for (j = 0u; j + 1u < count - i; j++) {
            int swap = 0;

            if (entries[j].type != entries[j + 1u].type) {
                swap = (entries[j].type != NK_VFS_DIR);
            } else if (nk_sh_strlt(entries[j + 1u].name, entries[j].name)) {
                swap = 1;
            }
            if (swap != 0) {
                nk_vfs_dirent_t tmp = entries[j];

                entries[j] = entries[j + 1u];
                entries[j + 1u] = tmp;
            }
        }
    }
    for (i = 0u; i < count; i++) {
        if (entries[i].type == NK_VFS_DIR) {
            nk_sh_printf(ctx, "d  %s/\n", entries[i].name);
        } else {
            nk_sh_printf(ctx, "f  %s  %u\n", entries[i].name,
                         entries[i].size);
        }
    }
    if (count == 0u) {
        nk_sh_puts(ctx, "(empty)\n");
    }
}

static nk_status_t nk_sh_open_for_read(nk_shell64_ctx_t *ctx, const char *path,
                                       char *resolved, uint32_t cap)
{
    nk_vfs_stat_t st;

    if (ctx->read == (nk_status_t (*)(const char *, uint32_t, void *,
                                      uint32_t, uint32_t *))0) {
        nk_sh_puts(ctx, "unavailable\n");
        return NK_STATUS_UNSUPPORTED;
    }
    if (nk_shell64_resolve(ctx, path, resolved, cap) != NK_STATUS_OK) {
        nk_sh_puts(ctx, "bad path\n");
        return NK_STATUS_INVALID_ARGUMENT;
    }
    if (ctx->stat != (nk_status_t (*)(const char *, nk_vfs_stat_t *))0) {
        if (ctx->stat(resolved, &st) != NK_STATUS_OK) {
            nk_sh_printf(ctx, "%s: not found\n", resolved);
            return NK_STATUS_NOT_FOUND;
        }
        if (st.type != NK_VFS_FILE) {
            nk_sh_printf(ctx, "%s: is a directory\n", resolved);
            return NK_STATUS_INVALID_ARGUMENT;
        }
    }
    return NK_STATUS_OK;
}

static void nk_sh_cmd_cat(nk_shell64_ctx_t *ctx, const char *path)
{
    uint8_t buf[NK_SH_CAT_CHUNK];
    char resolved[NK_VFS_PATH_MAX];
    uint32_t offset = 0u;

    if (nk_sh_open_for_read(ctx, path, resolved, (uint32_t)sizeof(resolved)) !=
        NK_STATUS_OK) {
        return;
    }
    for (;;) {
        uint32_t len = 0u;
        nk_status_t status = ctx->read(resolved, offset, buf,
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

static void nk_sh_cmd_head(nk_shell64_ctx_t *ctx, uint32_t argc, char **argv)
{
    uint8_t buf[NK_SH_CAT_CHUNK];
    char resolved[NK_VFS_PATH_MAX];
    uint32_t want = 10u;
    uint32_t lines = 0u;
    uint32_t offset = 0u;
    uint32_t i;
    int done = 0;

    if (argc < 2u) {
        nk_sh_puts(ctx, "usage: head <path> [lines]\n");
        return;
    }
    if (argc >= 3u && nk_sh_parse_dec(argv[2], &want) != 0) {
        nk_sh_puts(ctx, "head: bad line count\n");
        return;
    }
    if (want == 0u) {
        want = 1u;
    }
    if (nk_sh_open_for_read(ctx, argv[1], resolved, (uint32_t)sizeof(resolved)) !=
        NK_STATUS_OK) {
        return;
    }
    while (!done) {
        uint32_t len = 0u;

        if (ctx->read(resolved, offset, buf, (uint32_t)sizeof(buf), &len) !=
                NK_STATUS_OK ||
            len == 0u) {
            break;
        }
        for (i = 0u; i < len; i++) {
            nk_sh_putc(ctx, (char)buf[i]);
            if (buf[i] == '\n') {
                lines++;
                if (lines >= want) {
                    done = 1;
                    break;
                }
            }
        }
        offset += len;
    }
    if (lines != 0u && done == 0) {
        {
            /* File without a trailing newline: keep the next prompt clean. */
            uint8_t last = 0u;
            uint32_t probe = 0u;

            if (ctx->read(resolved, offset > 0u ? offset - 1u : 0u, &last, 1u,
                          &probe) == NK_STATUS_OK &&
                probe == 1u && last != (uint8_t)'\n') {
                nk_sh_puts(ctx, "\n");
            }
        }
    }
}

static void nk_sh_cmd_wc(nk_shell64_ctx_t *ctx, const char *path)
{
    uint8_t buf[NK_SH_CAT_CHUNK];
    char resolved[NK_VFS_PATH_MAX];
    uint32_t offset = 0u;
    uint32_t lines = 0u;
    uint32_t words = 0u;
    uint32_t bytes = 0u;
    int in_word = 0;

    if (path == (const char *)0) {
        nk_sh_puts(ctx, "usage: wc <path>\n");
        return;
    }
    if (nk_sh_open_for_read(ctx, path, resolved, (uint32_t)sizeof(resolved)) !=
        NK_STATUS_OK) {
        return;
    }
    for (;;) {
        uint32_t len = 0u;
        uint32_t i;

        if (ctx->read(resolved, offset, buf, (uint32_t)sizeof(buf), &len) !=
                NK_STATUS_OK ||
            len == 0u) {
            break;
        }
        for (i = 0u; i < len; i++) {
            char c = (char)buf[i];

            if (c == '\n') {
                lines++;
            }
            if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
                in_word = 0;
            } else if (in_word == 0) {
                in_word = 1;
                words++;
            }
        }
        bytes += len;
        offset += len;
        if (len < (uint32_t)sizeof(buf)) {
            break;
        }
    }
    nk_sh_printf(ctx, "%u lines  %u words  %u bytes  %s\n", lines, words,
                 bytes, resolved);
}

static void nk_sh_cmd_grep(nk_shell64_ctx_t *ctx, uint32_t argc, char **argv)
{
    uint8_t buf[NK_SH_CAT_CHUNK];
    char resolved[NK_VFS_PATH_MAX];
    char line[NK_SH_LINE_BUF];
    uint32_t offset = 0u;
    uint32_t line_len = 0u;
    uint32_t number = 1u;
    uint32_t matches = 0u;
    int truncated = 0;

    if (argc < 3u) {
        nk_sh_puts(ctx, "usage: grep <pattern> <path>\n");
        return;
    }
    if (nk_sh_open_for_read(ctx, argv[2], resolved, (uint32_t)sizeof(resolved)) !=
        NK_STATUS_OK) {
        return;
    }
    line[0] = '\0';
    for (;;) {
        uint32_t len = 0u;
        uint32_t i;

        if (ctx->read(resolved, offset, buf, (uint32_t)sizeof(buf), &len) !=
                NK_STATUS_OK ||
            len == 0u) {
            break;
        }
        for (i = 0u; i < len; i++) {
            char c = (char)buf[i];

            if (c == '\n') {
                line[line_len] = '\0';
                if (nk_sh_contains(line, argv[1]) != 0) {
                    nk_sh_printf(ctx, "%u: %s%s\n", number, line,
                                 truncated ? "..." : "");
                    matches++;
                }
                line_len = 0u;
                truncated = 0;
                number++;
                continue;
            }
            if (line_len + 1u < sizeof(line)) {
                line[line_len++] = c;
            } else {
                truncated = 1;
            }
        }
        offset += len;
        if (len < (uint32_t)sizeof(buf)) {
            break;
        }
    }
    if (line_len != 0u) {
        line[line_len] = '\0';
        if (nk_sh_contains(line, argv[1]) != 0) {
            nk_sh_printf(ctx, "%u: %s%s\n", number, line,
                         truncated ? "..." : "");
            matches++;
        }
    }
    nk_sh_printf(ctx, "%u match%s\n", matches, (matches == 1u) ? "" : "es");
}

static void nk_sh_cmd_stat(nk_shell64_ctx_t *ctx, const char *path)
{
    char resolved[NK_VFS_PATH_MAX];
    nk_vfs_stat_t st;

    if (ctx->stat == (nk_status_t (*)(const char *, nk_vfs_stat_t *))0) {
        nk_sh_puts(ctx, "unavailable\n");
        return;
    }
    if (path == (const char *)0) {
        nk_sh_puts(ctx, "usage: stat <path>\n");
        return;
    }
    if (nk_shell64_resolve(ctx, path, resolved, (uint32_t)sizeof(resolved)) !=
        NK_STATUS_OK) {
        nk_sh_puts(ctx, "stat: bad path\n");
        return;
    }
    if (ctx->stat(resolved, &st) != NK_STATUS_OK) {
        nk_sh_printf(ctx, "stat: %s: not found\n", resolved);
        return;
    }
    nk_sh_printf(ctx, "%s\n  type: %s\n  size: %u bytes\n  id:   %u\n",
                 resolved, (st.type == NK_VFS_DIR) ? "directory" : "file",
                 st.size, st.id);
}

static void nk_sh_cmd_history(nk_shell64_ctx_t *ctx, uint32_t argc,
                              char **argv)
{
    uint32_t want = nk_shell64_history_count(ctx);
    uint32_t total = nk_shell64_history_count(ctx);
    uint32_t i;

    if (argc >= 2u) {
        if (nk_sh_parse_dec(argv[1], &want) != 0) {
            nk_sh_puts(ctx, "history: bad count\n");
            return;
        }
        if (want > total) {
            want = total;
        }
    }
    if (total == 0u) {
        nk_sh_puts(ctx, "(no history)\n");
        return;
    }
    for (i = want; i > 0u; i--) {
        const char *entry = nk_shell64_history_at(ctx, i - 1u);

        if (entry != (const char *)0) {
            nk_sh_printf(ctx, "%u  %s\n", total - (i - 1u), entry);
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
    char resolved[NK_VFS_PATH_MAX];
    nk_status_t status;

    if (ctx->exec == (nk_status_t (*)(const char *))0) {
        nk_sh_puts(ctx, "unavailable\n");
        return;
    }
    if (nk_shell64_resolve(ctx, path, resolved, (uint32_t)sizeof(resolved)) !=
        NK_STATUS_OK) {
        nk_sh_puts(ctx, "exec: bad path\n");
        return;
    }
    status = ctx->exec(resolved);
    nk_sh_printf(ctx, "exec %s: %u\n", resolved, status);
}
/*
static void nk_sh_cmd_ui(nk_shell64_ctx_t *ctx, uint32_t argc, char **argv)
{
    nk_status_t status;

    if (argc < 2u) {
        nk_sh_puts(ctx, "usage: ui start | ui lvgl\n");
        return;
    }
    if (nk_sh_streq(argv[1], "lvgl")) {
         // * LVGL claims the whole framebuffer, so it is mutually exclusive
         // with the InterfaceKit desktop: whichever starts first owns it.
        if (ctx->ui_lvgl == (nk_status_t (*)(void))0) {
            nk_sh_puts(ctx, "ui: LVGL desktop unavailable in this shell\n");
            return;
        }
        status = ctx->ui_lvgl();
        if (status == NK_STATUS_OK) {
            nk_sh_puts(ctx, "ui: LVGL desktop started\n");
        } else if (status == NK_STATUS_ALREADY_EXISTS) {
            nk_sh_puts(ctx, "ui: LVGL desktop is already running\n");
        } else {
            nk_sh_printf(ctx, "ui lvgl: %u\n", (uint32_t)status);
        }
        return;
    }
    if (!nk_sh_streq(argv[1], "start")) {
        nk_sh_puts(ctx, "usage: ui start | ui lvgl\n");
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

*/
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
    } else if (nk_sh_streq(argv[0], "sysinfo")) {
        nk_sh_cmd_sysinfo(ctx);
    } else if (nk_sh_streq(argv[0], "uptime")) {
        nk_sh_cmd_uptime(ctx, 1);
    } else if (nk_sh_streq(argv[0], "pwd")) {
        nk_sh_cmd_pwd(ctx);
    } else if (nk_sh_streq(argv[0], "cd")) {
        nk_sh_cmd_cd(ctx, (argc > 1u) ? argv[1] : (const char *)0);
    } else if (nk_sh_streq(argv[0], "ls")) {
        nk_sh_cmd_ls(ctx, (argc > 1u) ? argv[1] : (const char *)0);
    } else if (nk_sh_streq(argv[0], "cat")) {
        if (argc < 2u) {
            nk_sh_puts(ctx, "usage: cat <path>\n");
        } else {
            nk_sh_cmd_cat(ctx, argv[1]);
        }
    } else if (nk_sh_streq(argv[0], "head")) {
        nk_sh_cmd_head(ctx, argc, argv);
    } else if (nk_sh_streq(argv[0], "wc")) {
        nk_sh_cmd_wc(ctx, (argc > 1u) ? argv[1] : (const char *)0);
    } else if (nk_sh_streq(argv[0], "grep")) {
        nk_sh_cmd_grep(ctx, argc, argv);
    } else if (nk_sh_streq(argv[0], "stat")) {
        nk_sh_cmd_stat(ctx, (argc > 1u) ? argv[1] : (const char *)0);
    } else if (nk_sh_streq(argv[0], "history")) {
        nk_sh_cmd_history(ctx, argc, argv);
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
    } else if (nk_sh_streq(argv[0], "usb")) {
        nk_sh_cmd_usb(ctx, argc, argv);
    } else if (nk_sh_streq(argv[0], "clear")) {
        if (ctx->clear != (void (*)(void))0) {
            ctx->clear();
        } else {
            nk_sh_puts(ctx, "\n\n\n\n\n\n\n\n");
        }
    } else if (nk_sh_streq(argv[0], "exit") || nk_sh_streq(argv[0], "quit")) {
        return 1;
    } else {
        nk_sh_printf(ctx, "unknown command: %s\n", argv[0]);
    }
    return 0;
}
