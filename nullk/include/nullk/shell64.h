/*
 * The nk) shell command engine. The same parser runs in the tiny kernel
 * fallback shell and in the userspace shell; only the callbacks differ.
 *
 * Kernel mode: callbacks are backed directly by the VFS.
 * Userspace mode: callbacks are backed by ABI v3 syscalls.
 */
#ifndef NULLK_SHELL64_H
#define NULLK_SHELL64_H

#include <stdint.h>
#include "sandbox.h"
#include "vfs.h"

#define NK_SHELL_LINE_MAX      256u
#define NK_SHELL_HISTORY_MAX   16u

/* Mirrors the framebuffer console's NKFB_GUI_* requests. The shared engine must
 * not include a port header, so it passes plain numbers through ctx->gui. */
#define NK_SHELL_GUI_DEMO       0
#define NK_SHELL_GUI_CLOSE      1
#define NK_SHELL_GUI_PANEL_ON   2
#define NK_SHELL_GUI_PANEL_OFF  3
#define NK_SHELL_GUI_REFRESH    4

typedef struct {
    /* Filesystem operations, supplied by the host of the shell. */
    nk_status_t (*stat)(const char *path, nk_vfs_stat_t *out);
    nk_status_t (*readdir)(const char *path, nk_vfs_dirent_t *out,
                           uint32_t max, uint32_t *count);
    nk_status_t (*read)(const char *path, uint32_t offset, void *buf,
                        uint32_t cap, uint32_t *out_len);
    nk_status_t (*exec)(const char *path);
    /* Optional gothicOS userland service hook behind `ui start`. */
    nk_status_t (*ui_start)(void);
    /* Optional LVGL desktop hook behind `ui lvgl`; claims the framebuffer. */
    nk_status_t (*ui_lvgl)(void);
    nk_status_t (*usb_read)(uint32_t lun, uint64_t lba, uint32_t count,
                            void *buf);
    /* Optional framebuffer GUI engine behind `gui ...`; receives one of the
     * NK_SHELL_GUI_* requests. */
    nk_status_t (*gui)(uint32_t command);
    /* Optional visible-console clear for `clear`; otherwise blank lines. */
    void (*clear)(void);

    /* Output sinks. */
    void (*putc)(char c);
    void (*puts)(const char *text, uint32_t len);

    /* Sandbox for the mem commands; kernel mode may grant a physical window. */
    nk_sandbox_t sandbox;

    /* Exposed by 'info'. */
    uint64_t ticks;
    uint32_t task_count;
    uint32_t flags;

    /* Current directory, normalised as "@", "@/data", ... */
    char cwd[NK_VFS_PATH_MAX];

    /* Command history ring used by the line editor and `history`. */
    char history[NK_SHELL_HISTORY_MAX][NK_SHELL_LINE_MAX];
    uint32_t history_count;
    uint32_t history_next;
} nk_shell64_ctx_t;

void nk_shell64_init(nk_shell64_ctx_t *ctx);
void nk_shell64_prompt(nk_shell64_ctx_t *ctx);

/* Execute one complete line (mutable buffer). Returns 0 to continue,
 * 1 when the shell should stop ('exit'). */
int nk_shell64_exec(nk_shell64_ctx_t *ctx, char *line);

/* Print the nk) banner/help text. */
void nk_shell64_banner(nk_shell64_ctx_t *ctx);
void nk_shell64_help(nk_shell64_ctx_t *ctx);

/* Resolve a user path against ctx->cwd into a normalised "@..." path. */
nk_status_t nk_shell64_resolve(const nk_shell64_ctx_t *ctx, const char *arg,
                               char *out, uint32_t cap);
const char *nk_shell64_cwd(const nk_shell64_ctx_t *ctx);

/* Command name table, NULL-terminated, for `help` and Tab completion. */
const char *nk_shell64_command_name(uint32_t index);

/* Line-editor history helpers. Index 0 is the most recent entry. */
void nk_shell64_history_push(nk_shell64_ctx_t *ctx, const char *line);
const char *nk_shell64_history_at(const nk_shell64_ctx_t *ctx,
                                  uint32_t index_from_newest);
uint32_t nk_shell64_history_count(const nk_shell64_ctx_t *ctx);

#endif /* NULLK_SHELL64_H */
