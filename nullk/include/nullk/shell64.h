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

#define NK_SHELL_LINE_MAX 256u

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
    nk_status_t (*usb_read)(uint32_t lun, uint64_t lba, uint32_t count,
                            void *buf);

    /* Output sinks. */
    void (*putc)(char c);
    void (*puts)(const char *text, uint32_t len);

    /* Sandbox for the mem commands; kernel mode may grant a physical window. */
    nk_sandbox_t sandbox;

    /* Exposed by 'info'. */
    uint64_t ticks;
    uint32_t task_count;
    uint32_t flags;
} nk_shell64_ctx_t;

void nk_shell64_init(nk_shell64_ctx_t *ctx);
void nk_shell64_prompt(nk_shell64_ctx_t *ctx);

/* Execute one complete line (mutable buffer). Returns 0 to continue,
 * 1 when the shell should stop ('exit'). */
int nk_shell64_exec(nk_shell64_ctx_t *ctx, char *line);

/* Print the nk) banner/help text. */
void nk_shell64_banner(nk_shell64_ctx_t *ctx);
void nk_shell64_help(nk_shell64_ctx_t *ctx);

#endif /* NULLK_SHELL64_H */
