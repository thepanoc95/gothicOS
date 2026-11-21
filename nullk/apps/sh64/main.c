/*
 * sh64 - the userspace nk) shell task.
 *
 * The command engine (shared/shell64.c) is identical to the kernel fallback
 * shell. Only its callbacks differ: here every filesystem, execution, memory
 * and USB operation is an ABI v3 syscall from ring 3.
 */
#include <stdint.h>

#include "libnk64.h"
#include "nullk/printk.h"
#include "nullk/shell64.h"

#define SH_SANDBOX_BYTES (64u * 1024u)

static nk_shell64_ctx_t shell_ctx;

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

static nk_status_t sh_exec(const char *path)
{
    return (nk_status_t)nk64_exec(path);
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

static void read_line(char *line, uint32_t cap)
{
    uint32_t len = 0;

    line[0] = '\0';
    for (;;) {
        uint8_t c = 0;
        int64_t n = nk64_read(0, &c, 1);

        if (n <= 0) {
            /* UART is non-blocking; do not monopolise the CPU while waiting. */
            (void)nk64_yield();
            continue;
        }
        if (c == '\n' || c == '\r') {
            line[len] = '\0';
            (void)nk64_write(1, "\n", 1);
            return;
        }
        if (c == 0x08u || c == 0x7fu) {
            if (len != 0) {
                len--;
                (void)nk64_write(1, "\b \b", 3);
            }
            continue;
        }
        if (c < 0x20u) {
            continue;
        }
        if (len + 1 < cap) {
            line[len++] = (char)c;
            (void)nk64_write(1, &c, 1);
        }
    }
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
    shell_ctx.usb_read = sh_usb_read;
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
