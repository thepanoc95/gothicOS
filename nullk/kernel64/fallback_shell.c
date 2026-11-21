/*
 * Tiny AMD64 kernel core - ring-0 fallback shell.
 *
 * Used only when no userspace init ELF can be loaded. The command parser is
 * shared with the userspace shell (nk_shell64_exec); this file supplies VFS
 * and UART callbacks plus two loader-only commands:
 *
 *   load      - receive an ELF over the polling UART protocol and exec it
 *   usbload   - read a NKUSBPRG sector from LUN and exec the payload ELF
 *
 * The sandbox window is at NK64_SANDBOX_BASE; addresses printed here are
 * expressed as base + offset so the offsets shown by the shared engine map
 * directly to the granted window.
 */
#include <stdint.h>

/* kernel64.h contains a nested-comment typo we do not own. */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wcomment"
#include "nullk/kernel64.h"
#pragma GCC diagnostic pop
#include "nullk/abi.h"
#include "nullk/printk.h"
#include "nullk/vfs.h"
#include "nullk/shell64.h"
#include "nullk/sandbox.h"
#include "nullk/usb_as_sata.h"

#define NK64_ELF_MAX 0x80000u

/* From main.c / syscall.c / uartload.c. */
extern uint8_t nk64_elf_buffer[NK64_ELF_MAX];
int nk64_usb_backend_read(uint32_t lun, uint64_t lba, uint32_t count,
                          void *buf);
int nk64_task_create_elf(const uint8_t *elf, uint32_t size, const char *name);
int nk64_uart_receive_elf(uint8_t *dest, uint32_t cap, uint32_t *out_len);

static nk_shell64_ctx_t g_shell;
static uint8_t g_usb_sector[NK_UAS_BLOCK_SIZE];

/* Avoid hard-coded string lengths for UART output. */
static uint32_t nk64_fb_strlen(const char *s)
{
    uint32_t n = 0u;

    while (s[n] != '\0') {
        n++;
    }
    return n;
}

#define FB_PUTS(s) nk64_uart_puts((s), nk64_fb_strlen(s))

/* ------------------------------------------------------------------ */
/* Callbacks for the shared shell engine.                              */
/* ------------------------------------------------------------------ */

static nk_status_t nk64_fb_exec(const char *path)
{
    uint32_t len = 0u;
    nk_status_t st;

    st = nk_vfs_read_file(path, 0u, nk64_elf_buffer, NK64_ELF_MAX, &len);
    if (st != NK_STATUS_OK) {
        return st;
    }
    if (len == 0u) {
        return NK_STATUS_NOT_FOUND;
    }
    if (nk64_task_create_elf(nk64_elf_buffer, len, "app") < 0) {
        return NK_STATUS_INTERNAL_ERROR;
    }
    return NK_STATUS_OK;
}

static nk_status_t nk64_fb_usb_read(uint32_t lun, uint64_t lba,
                                    uint32_t count, void *buf)
{
    int rc = nk64_usb_backend_read(lun, lba, count, buf);

    return (rc == (int)NK_STATUS_OK) ? NK_STATUS_OK : (nk_status_t)rc;
}

static uint32_t nk64_fb_task_count(void)
{
    uint32_t count = 0u;
    uint32_t i;

    for (i = 0u; i < NK64_MAX_TASKS; i++) {
        if (nk64_task_alive((int)i)) {
            count++;
        }
    }
    return count;
}

/* ------------------------------------------------------------------ */
/* Input / command helpers.                                            */
/* ------------------------------------------------------------------ */

static uint32_t nk64_fb_read_line(char *buf, uint32_t cap)
{
    uint32_t len = 0u;

    if (cap == 0u) {
        return 0u;
    }
    for (;;) {
        int c = nk64_uart_getc();

        if (c < 0) {
            continue;                 /* interactive: block until a byte */
        }
        if (c == '\n' || c == '\r') {
            nk64_uart_putc('\n');
            break;
        }
        if (c == '\b' || c == 127) {
            if (len != 0u) {
                len--;
                nk64_uart_putc('\b');
                nk64_uart_putc(' ');
                nk64_uart_putc('\b');
            }
            continue;
        }
        if (len + 1u >= cap) {
            continue;
        }
        buf[len++] = (char)c;
        nk64_uart_putc((char)c);
    }
    buf[len] = '\0';
    return len;
}

static const char *nk64_fb_skip_spaces(const char *s)
{
    while (*s == ' ' || *s == '\t') {
        s++;
    }
    return s;
}

static int nk64_fb_command(const char *line, const char *cmd)
{
    const char *p = nk64_fb_skip_spaces(line);
    uint32_t i = 0u;

    while (cmd[i] != '\0') {
        if (p[i] != cmd[i]) {
            return 0;
        }
        i++;
    }
    return (p[i] == '\0' || p[i] == ' ' || p[i] == '\t');
}

static uint32_t nk64_fb_arg_lun(const char *line)
{
    const char *p = nk64_fb_skip_spaces(line);
    uint32_t lun = 0u;
    int seen = 0;

    /* Skip the command word first, then parse the optional LUN argument. */
    while (*p != '\0' && *p != ' ' && *p != '\t') {
        p++;
    }
    p = nk64_fb_skip_spaces(p);
    while (*p != '\0' && *p != ' ' && *p != '\t') {
        if (*p >= '0' && *p <= '9') {
            lun = lun * 10u + (uint32_t)(*p - '0');
            seen = 1;
        }
        p++;
    }
    return (seen != 0) ? lun : 0u;
}

/* ------------------------------------------------------------------ */
/* Loader-only commands.                                               */
/* ------------------------------------------------------------------ */

static void nk64_fb_cmd_load(void)
{
    uint32_t len = 0u;

    FB_PUTS("nk: waiting for UART ELF (NKELFRX)...\n");
    if (nk64_uart_receive_elf(nk64_elf_buffer, NK64_ELF_MAX, &len) != 0) {
        FB_PUTS("nk: UART receive failed\n");
        return;
    }
    if (nk64_task_create_elf(nk64_elf_buffer, len, "uartload") >= 0) {
        FB_PUTS("nk: launching UART image\n");
        nk64_task_start_first();       /* noreturn */
    }
    FB_PUTS("nk: UART image rejected\n");
}

static void nk64_fb_cmd_usbload(const char *line)
{
    const nk_usbprog_header_t *hdr =
        (const nk_usbprog_header_t *)(const void *)g_usb_sector;
    uint32_t lun = nk64_fb_arg_lun(line);
    uint32_t sectors;
    uint32_t len;
    uint32_t i;

    if (nk64_fb_usb_read(lun, NK_USBPROG_SECTOR, 1u, g_usb_sector) !=
        NK_STATUS_OK) {
        FB_PUTS("nk: usbload: cannot read program header\n");
        return;
    }
    if (nk_usbprog_valid(hdr) == 0) {
        FB_PUTS("nk: usbload: bad NKUSBPRG header\n");
        return;
    }
    len = hdr->elf_size;
    if (len == 0u || len > NK64_ELF_MAX) {
        FB_PUTS("nk: usbload: bad ELF size\n");
        return;
    }
    sectors = (len + (uint32_t)NK_UAS_BLOCK_SIZE - 1u) /
              (uint32_t)NK_UAS_BLOCK_SIZE;
    for (i = 0u; i < sectors; i++) {
        if (nk64_fb_usb_read(lun, hdr->elf_lba + (uint64_t)i, 1u,
                             &nk64_elf_buffer[i * NK_UAS_BLOCK_SIZE]) !=
            NK_STATUS_OK) {
            FB_PUTS("nk: usbload: ELF read failed\n");
            return;
        }
    }
    if (nk64_task_create_elf(nk64_elf_buffer, len, "usbload") >= 0) {
        FB_PUTS("nk: launching USB image\n");
        nk64_task_start_first();       /* noreturn */
    }
    FB_PUTS("nk: USB image rejected\n");
}

/* ------------------------------------------------------------------ */
/* Public entry point.                                                 */
/* ------------------------------------------------------------------ */

void nk64_fallback_shell(void)
{
    char line[NK_SHELL_LINE_MAX];

    nk_shell64_init(&g_shell);
    g_shell.stat = nk_vfs_stat;
    g_shell.readdir = nk_vfs_readdir;
    g_shell.read = nk_vfs_read_file;
    g_shell.exec = nk64_fb_exec;
    g_shell.usb_read = nk64_fb_usb_read;
    g_shell.putc = nk64_uart_putc;
    g_shell.puts = nk64_uart_puts;
    (void)nk_sandbox_init(&g_shell.sandbox, NK64_SANDBOX_BASE,
                          NK64_SANDBOX_SIZE, 0u);

    /* Print the translated sandbox window once; the shared engine prints
     * command output as offsets inside this window. */
    nk_printk(NK_KERN_INFO,
              "nk: sandbox window 0x%08x%08x + offset (size %u KiB)\n",
              (uint32_t)(NK64_SANDBOX_BASE >> 32),
              (uint32_t)NK64_SANDBOX_BASE,
              (uint32_t)(NK64_SANDBOX_SIZE / 1024u));

    nk_shell64_banner(&g_shell);

    for (;;) {
        int stop;

        g_shell.ticks = nk64_ticks;
        g_shell.task_count = nk64_fb_task_count();
        g_shell.flags = 0u;

        nk_shell64_prompt(&g_shell);   /* prints exactly "nk) " */
        nk64_fb_read_line(line, (uint32_t)sizeof(line));

        if (line[0] == '\0') {
            continue;
        }
        if (nk64_fb_command(line, "load")) {
            nk64_fb_cmd_load();
            continue;
        }
        if (nk64_fb_command(line, "usbload")) {
            nk64_fb_cmd_usbload(line);
            continue;
        }
        stop = nk_shell64_exec(&g_shell, line);
        if (stop != 0) {
            /* The fallback shell is the last resort: 'exit' just re-prompts
             * instead of returning into an undefined caller. */
            FB_PUTS("nk: exit ignored in fallback shell\n");
        }
    }
}
