/*
 * sh64 - the userspace nk) shell task.
 *
 * The command engine (shared/shell64.c) is identical to the kernel fallback
 * shell. Only its callbacks differ: here every filesystem, execution, memory
 * and USB operation is an ABI v3 syscall from ring 3.
 */
#include <stdint.h>

#include "libnk64.h"
#include "nullk/interfacekit_server.h"
#include "nullk/printk.h"
#include "nullk/securekernel.h"
#include "nullk/shell64.h"

#define SH_SANDBOX_BYTES (64u * 1024u)
#define SH_READ_CHUNK 2048u
#define SH_POLL_SPINS 2048u

/* The framebuffer console lives in the Linux port. The AMD64 build leaves
 * these weak references unresolved, so `gui`/`clear` degrade gracefully. */
#if defined(NULLK_LINUX_PORT)
extern int nk64_fb_console_gui(int command) __attribute__((weak));
extern void nk64_fb_console_clear(void) __attribute__((weak));
#endif

static nk_shell64_ctx_t shell_ctx;
static uint8_t g_exec_image[NK_SECUREKERNEL_MAX_IMAGE];
static uint32_t g_ui_started;
static uint32_t g_ui_lvgl_started;

static nk_status_t sh_stat(const char *path, nk_vfs_stat_t *out)
{
    return (nk_status_t)nk64_stat(path, out);
}

static nk_status_t sh_readdir(const char *path, nk_vfs_dirent_t *out,
                              uint32_t max, uint32_t *count)
{
    nk_vfs_stat_t st;

    if (count != NULL) {
        *count = 0;
    }
    if (nk64_stat(path, &st) != 0 || st.type != NK_VFS_DIR) {
        return NK_STATUS_NOT_FOUND;
    }
    uint64_t n = nk64_readdir(path, out, max);
    if (count != NULL) {
        *count = (uint32_t)n;
    }
    return n <= max ? NK_STATUS_OK : NK_STATUS_INVALID_ARGUMENT;
}

static nk_status_t sh_read(const char *path, uint32_t offset, void *buf,
                           uint32_t cap, uint32_t *out_len)
{
    uint8_t scratch[64];
    nk_vfs_stat_t st;
    uint64_t handle;
    int64_t got;

    if (out_len != NULL) {
        *out_len = 0;
    }
    if (cap == 0) {
        return NK_STATUS_OK;
    }
    /* OPEN failure collides with the first valid handle, so check first. */
    if (nk64_stat(path, &st) != 0 || st.type != NK_VFS_FILE) {
        return NK_STATUS_NOT_FOUND;
    }
    handle = nk64_open(path, NK64_OPEN_READ);
    if (handle == 0) {
        return NK_STATUS_NOT_FOUND;
    }

    /* The ABI exposes byte streams, so seek by reading and discarding. */
    while (offset != 0) {
        uint32_t chunk = offset < sizeof(scratch) ? offset :
                         (uint32_t)sizeof(scratch);
        if (nk64_read((uint32_t)handle, scratch, chunk) <= 0) {
            break;
        }
        offset -= chunk;
    }

    got = nk64_read((uint32_t)handle, buf, cap);
    (void)nk64_close(handle);
    if (got < 0) {
        got = 0;
    }
    if (out_len != NULL) {
        *out_len = (uint32_t)got;
    }
    return NK_STATUS_OK;
}

/* Read a whole file into the SecureKernel staging buffer. ABI reads are capped
 * at 4 KiB, so use smaller chunks and advance the handle each time. */
static nk_status_t sh_read_all(const char *path, uint8_t *buf, uint32_t cap,
                               uint32_t *out_len)
{
    nk_vfs_stat_t st;
    uint64_t handle;
    uint32_t total = 0u;

    if (out_len != NULL) {
        *out_len = 0u;
    }
    if (nk64_stat(path, &st) != 0 || st.type != NK_VFS_FILE) {
        return NK_STATUS_NOT_FOUND;
    }
    if (st.size > cap) {
        return NK_STATUS_INVALID_ARGUMENT;
    }
    handle = nk64_open(path, NK64_OPEN_READ);
    if (handle == 0) {
        return NK_STATUS_NOT_FOUND;
    }
    while (total < cap) {
        uint32_t chunk = cap - total;
        int64_t got;

        if (chunk > SH_READ_CHUNK) {
            chunk = SH_READ_CHUNK;
        }
        got = nk64_read((uint32_t)handle, buf + total, chunk);
        if (got <= 0) {
            break;
        }
        total += (uint32_t)got;
    }
    (void)nk64_close(handle);
    if (out_len != NULL) {
        *out_len = total;
    }
    return NK_STATUS_OK;
}

static nk_status_t sh_exec(const char *path)
{
    nk_secure_report_t report;
    uint32_t len = 0u;
    nk_status_t status;
    int rc;

    status = sh_read_all(path, g_exec_image, (uint32_t)sizeof(g_exec_image),
                         &len);
    if (status != NK_STATUS_OK) {
        return status;
    }
    rc = nk_securekernel_enforce(NK_SECURE_SUBJECT_SHELL, path,
                                 g_exec_image, len, &report);
    if (rc != 0) {
        nk_printf("[nksh] SecureKernel rejected verification request (%u)\n",
                  (uint32_t)rc);
        return NK_STATUS_INTERNAL_ERROR;
    }
    if (report.decision != (uint32_t)NK_SECURE_DECISION_ALLOW) {
        if ((report.flags & (uint32_t)NK_SECURE_FLAG_POLICY_DENY) != 0u) {
            nk_printf("[nksh] SecureKernel policy denied %s: %s\n",
                      path, report.detail);
        } else {
            nk_printf("[nksh] SecureKernel blocked %s: %s\n",
                      path, report.detail);
        }
        return NK_STATUS_PERMISSION_DENIED;
    }
    if ((report.flags & (uint32_t)NK_SECURE_FLAG_UNSIGNED) != 0u) {
        nk_printf("[nksh] SecureKernel warning: %s\n", report.detail);
    }
    /* Exec the bytes we just verified, not the path again: no check/use gap. */
    return (nk_status_t)nk64_exec_mem(g_exec_image, len);
}

static nk_status_t sh_ui_start(void)
{
    uint64_t id;

    if (g_ui_started != 0u) {
        return NK_STATUS_ALREADY_EXISTS;
    }
    /* Desktop policy belongs to gothicOS userland, not to init or ring 0. */
    id = nk64_task_create("InterfaceKit", interfacekit_server_entry, 0, 0);
    if (id == 0u) {
        nk_printf("[nksh] failed to start InterfaceKit service\n");
        return NK_STATUS_INTERNAL_ERROR;
    }
    g_ui_started = 1u;
    nk_printf("[nksh] InterfaceKit task id=%u\n", (uint32_t)id);

    id = nk64_task_create("InterfaceKitDemo", interfacekit_demo_entry, 0, 0);
    if (id == 0u) {
        nk_printf("[nksh] failed to start InterfaceKit demo service\n");
        return NK_STATUS_INTERNAL_ERROR;
    }
    nk_printf("[nksh] InterfaceKit demo task id=%u\n", (uint32_t)id);
    return NK_STATUS_OK;
}

static nk_status_t sh_ui_lvgl(void)
{
    uint64_t id;

    if (g_ui_lvgl_started != 0u) {
        return NK_STATUS_ALREADY_EXISTS;
    }
    id = nk64_task_create("lvgl64", lvgl_desktop_entry, 0, 0);
    if (id == 0u) {
        nk_printf("[nksh] failed to start the LVGL desktop\n");
        return NK_STATUS_INTERNAL_ERROR;
    }
    g_ui_lvgl_started = 1u;
    nk_printf("[nksh] LVGL desktop task id=%u\n", (uint32_t)id);
    return NK_STATUS_OK;
}

static nk_status_t sh_usb_read(uint32_t lun, uint64_t lba, uint32_t count,
                               void *buf)
{
    return (nk_status_t)nk64_usb_read(lun, lba, count, buf);
}

static void sh_putc(char c)
{
    (void)nk64_write(1, &c, 1);
}

static void sh_puts(const char *text, uint32_t len)
{
    (void)nk64_write(1, text, len);
}

static void grant_sandbox(void)
{
    void *region = nk64_mmap(0, SH_SANDBOX_BYTES,
                             NK64_MM_SANDBOX | NK64_MM_USER);

    if (region == NULL) {
        const char *msg = "[nksh] sandbox grant failed; mem commands disabled";
        nk_printf("[nksh] sandbox grant failed; mem commands disabled\n");
        (void)nk64_log(NK_KERN_WARN, msg, (uint32_t)nk_strlen(msg));
        return;
    }
    if (nk_sandbox_init(&shell_ctx.sandbox, (uint64_t)(uintptr_t)region,
                        SH_SANDBOX_BYTES, 0) != 0) {
        const char *msg = "[nksh] sandbox init failed";
        nk_printf("[nksh] sandbox init failed\n");
        (void)nk64_log(NK_KERN_WARN, msg, (uint32_t)nk_strlen(msg));
    }
}

static void sh_out(const char *text, uint32_t len)
{
    if (text != NULL && len != 0u) {
        (void)nk64_write(1, text, len);
    }
}

/* Poll one byte. UART and the framebuffer queue are both non-blocking, so this
 * yields instead of spinning at full speed. */
static int sh_poll_byte(uint8_t *out, uint32_t spins)
{
    uint32_t i;

    for (i = 0u; i < spins; i++) {
        int64_t n = nk64_read(0, out, 1);

        if (n > 0) {
            return 1;
        }
        (void)nk64_yield();
    }
    return 0;
}

/* Repaint the edit line from column 0. `erase` adds one trailing blank so a
 * deleted character does not linger after the cursor moves back. */
static void sh_repaint_line(const char *line, uint32_t len, uint32_t pos,
                            uint32_t erase)
{
    uint32_t back = (len - pos) + erase;
    uint32_t i;

    sh_out("\r", 1u);
    sh_out("nk) ", 4u);
    sh_out(line, len);
    if (erase != 0u) {
        sh_out(" ", 1u);
    }
    for (i = 0u; i < back; i++) {
        sh_out("\b", 1u);
    }
}

static int sh_prefix(const char *text, const char *prefix, uint32_t plen)
{
    uint32_t i;

    for (i = 0u; i < plen; i++) {
        if (text[i] == '\0' || text[i] != prefix[i]) {
            return 0;
        }
    }
    return 1;
}

/* Tab completion for the command word only; it completes the common prefix and
 * appends a space when exactly one command matches. */
static void sh_complete_command(char *line, uint32_t *len, uint32_t *pos,
                                uint32_t cap)
{
    char common[NK_SHELL_LINE_MAX];
    uint32_t common_len = 0u;
    uint32_t matches = 0u;
    uint32_t i;
    const char *name;

    if (*pos != *len) {
        return;
    }
    for (i = 0u; line[i] != '\0'; i++) {
        if (line[i] == ' ') {
            return; /* path arguments are not completed yet */
        }
    }
    for (i = 0u; (name = nk_shell64_command_name(i)) != NULL; i++) {
        if (!sh_prefix(name, line, *len)) {
            continue;
        }
        matches++;
        if (matches == 1u) {
            uint32_t j = 0u;

            while (name[j] != '\0' && j + 1u < sizeof(common)) {
                common[j] = name[j];
                j++;
            }
            common[j] = '\0';
            common_len = j;
        } else {
            uint32_t j = 0u;

            while (j < common_len && name[j] != '\0' &&
                   common[j] == name[j]) {
                j++;
            }
            common_len = j;
            common[j] = '\0';
        }
    }
    if (matches == 0u || common_len <= *len) {
        return;
    }
    if (common_len + 2u > cap) {
        return;
    }
    for (i = *len; i < common_len; i++) {
        line[i] = common[i];
    }
    *len = common_len;
    line[*len] = '\0';
    if (matches == 1u) {
        line[(*len)++] = ' ';
        line[*len] = '\0';
    }
    *pos = *len;
    sh_repaint_line(line, *len, *pos, 0u);
}

static void sh_clear_console(void)
{
#if defined(NULLK_LINUX_PORT)
    if (nk64_fb_console_clear != NULL) {
        nk64_fb_console_clear();
        return;
    }
#endif
    sh_out("\n\n\n\n\n\n\n\n", 8u);
}

static void read_line(char *line, uint32_t cap)
{
    char stash[NK_SHELL_LINE_MAX];
    uint32_t len = 0u;
    uint32_t pos = 0u;
    int32_t hist = -1; /* -1 = editing a fresh line */
    uint8_t c;

    line[0] = '\0';
    stash[0] = '\0';
    for (;;) {
        uint32_t i;

        if (!sh_poll_byte(&c, SH_POLL_SPINS)) {
            continue;
        }

        if (c == '\n' || c == '\r') {
            line[len] = '\0';
            sh_out("\n", 1u);
            nk_shell64_history_push(&shell_ctx, line);
            return;
        }
        if (c == 0x1bu) {
            /* Escape sequence: arrows, Home/End, Delete. */
            uint8_t seq[2];

            if (!sh_poll_byte(&seq[0], SH_POLL_SPINS)) {
                continue;
            }
            if (seq[0] != '[' && seq[0] != 'O') {
                continue;
            }
            if (!sh_poll_byte(&seq[1], SH_POLL_SPINS)) {
                continue;
            }
            if (seq[1] == 'A' || seq[1] == 'B') {
                int32_t next = (seq[1] == 'A') ? hist + 1 : hist - 1;
                const char *entry;

                if (next < -1) {
                    next = -1;
                }
                entry = nk_shell64_history_at(&shell_ctx,
                                              (uint32_t)((next < 0) ? 0 : next));
                if (next >= 0 && entry == NULL) {
                    continue;
                }
                if (hist == -1) {
                    nk_memcpy(stash, line, len + 1u);
                }
                hist = next;
                if (hist < 0) {
                    uint32_t n = nk_strlen(stash);

                    for (i = 0u; i < n && i + 1u < cap; i++) {
                        line[i] = stash[i];
                    }
                    len = (i < cap) ? i : cap - 1u;
                } else {
                    const char *src = nk_shell64_history_at(&shell_ctx,
                                                            (uint32_t)hist);
                    uint32_t n = (src != NULL) ? nk_strlen(src) : 0u;

                    if (n + 1u >= cap) {
                        n = cap - 2u;
                    }
                    nk_memcpy(line, src, n);
                    len = n;
                }
                line[len] = '\0';
                pos = len;
                sh_repaint_line(line, len, pos, 1u);
            } else if (seq[1] == 'C') {
                if (pos < len) {
                    pos++;
                    sh_out(line + pos - 1u, 1u);
                }
            } else if (seq[1] == 'D') {
                if (pos > 0u) {
                    pos--;
                    sh_out("\b", 1u);
                }
            } else if (seq[1] == 'H') {
                pos = 0u;
                sh_repaint_line(line, len, pos, 0u);
            } else if (seq[1] == 'F') {
                pos = len;
                sh_repaint_line(line, len, pos, 0u);
            } else if (seq[1] == '3') {
                uint8_t tilde;

                if (!sh_poll_byte(&tilde, SH_POLL_SPINS) || tilde != '~') {
                    continue;
                }
                if (pos < len) {
                    for (i = pos; i + 1u < len; i++) {
                        line[i] = line[i + 1u];
                    }
                    len--;
                    line[len] = '\0';
                    sh_repaint_line(line, len, pos, 1u);
                }
            }
            continue;
        }
        if (c == 0x08u || c == 0x7fu) {
            if (pos != 0u) {
                for (i = pos - 1u; i + 1u < len; i++) {
                    line[i] = line[i + 1u];
                }
                len--;
                pos--;
                line[len] = '\0';
                sh_repaint_line(line, len, pos, 1u);
            }
            continue;
        }
        if (c == 0x01u) { /* Ctrl+A */
            pos = 0u;
            sh_repaint_line(line, len, pos, 0u);
            continue;
        }
        if (c == 0x05u) { /* Ctrl+E */
            pos = len;
            sh_repaint_line(line, len, pos, 0u);
            continue;
        }
        if (c == 0x03u) { /* Ctrl+C: abandon the line */
            sh_out("^C\n", 3u);
            line[0] = '\0';
            return;
        }
        if (c == 0x0cu) { /* Ctrl+L: clear and redraw the prompt */
            sh_clear_console();
            sh_repaint_line(line, len, pos, 0u);
            continue;
        }
        if (c == 0x0bu) { /* Ctrl+K: kill to end */
            len = pos;
            line[len] = '\0';
            sh_repaint_line(line, len, pos, 1u);
            continue;
        }
        if (c == 0x15u) { /* Ctrl+U: kill the whole line */
            len = 0u;
            pos = 0u;
            line[0] = '\0';
            sh_repaint_line(line, len, pos, 1u);
            continue;
        }
        if (c == 0x17u) { /* Ctrl+W: delete the previous word */
            while (pos > 0u && line[pos - 1u] == ' ') {
                for (i = pos - 1u; i + 1u < len; i++) {
                    line[i] = line[i + 1u];
                }
                len--;
                pos--;
            }
            while (pos > 0u && line[pos - 1u] != ' ') {
                for (i = pos - 1u; i + 1u < len; i++) {
                    line[i] = line[i + 1u];
                }
                len--;
                pos--;
            }
            line[len] = '\0';
            sh_repaint_line(line, len, pos, 1u);
            continue;
        }
        if (c == '\t') {
            sh_complete_command(line, &len, &pos, cap);
            continue;
        }
        if (c < 0x20u) {
            continue;
        }
        if (len + 2u >= cap) {
            continue;
        }
        for (i = len; i > pos; i--) {
            line[i] = line[i - 1u];
        }
        line[pos++] = (char)c;
        len++;
        line[len] = '\0';
        if (pos == len) {
            sh_out((const char *)&c, 1u);
        } else {
            sh_repaint_line(line, len, pos, 0u);
        }
    }
}

static nk_status_t sh_gui(uint32_t command)
{
#if defined(NULLK_LINUX_PORT)
    if (nk64_fb_console_gui != NULL && nk64_fb_console_gui((int)command) == 0) {
        return NK_STATUS_OK;
    }
#else
    (void)command;
#endif
    return NK_STATUS_UNSUPPORTED;
}

static void sh_clear(void)
{
    sh_clear_console();
}

void shell_service_entry(void)
{
    char line[NK_SHELL_LINE_MAX];
    nk64_info_t info;

    /* nk_shell64_init() clears the context, so call it before filling it. */
    nk_shell64_init(&shell_ctx);
    shell_ctx.stat = sh_stat;
    shell_ctx.readdir = sh_readdir;
    shell_ctx.read = sh_read;
    shell_ctx.exec = sh_exec;
    shell_ctx.ui_start = sh_ui_start;
    shell_ctx.ui_lvgl = sh_ui_lvgl;
    shell_ctx.usb_read = sh_usb_read;
    shell_ctx.gui = sh_gui;
    shell_ctx.clear = sh_clear;
    shell_ctx.putc = sh_putc;
    shell_ctx.puts = sh_puts;

    nk_memset(&info, 0, sizeof(info));
    if (nk64_getinfo(&info) == 0) {
        shell_ctx.ticks = info.ticks;
        shell_ctx.task_count = info.task_count;
        shell_ctx.flags = info.flags;
    }
    grant_sandbox();

    nk_printf("\n[nksh] null_kernel shell running in ring 3\n");
    nk_shell64_banner(&shell_ctx);

    for (;;) {
        nk_shell64_prompt(&shell_ctx);
        read_line(line, sizeof(line));
        if (nk_shell64_exec(&shell_ctx, line) != 0) {
            nk64_exit(0);
        }
    }
}
