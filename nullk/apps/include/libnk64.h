/*
 * libnk64 - freestanding AMD64 userspace runtime for NULLK ABI v3.
 *
 * No libc, no POSIX, no dynamic linking. Every function here is either a thin
 * inline-asm wrapper around the `syscall` instruction or a pure in-task
 * helper. The kernel exports only CPU, memory, interrupt, IPC and scheduling
 * primitives; filesystems, shells and the USB/SATA service are ring 3 tasks.
 *
 * Syscall convention: rax = number, args = rdi/rsi/rdx/r10/r8/r9.
 */
#ifndef NULLK_LIBNK64_H
#define NULLK_LIBNK64_H

#include <stddef.h>
#include <stdint.h>

#include "nullk/abi64.h"
#include "nullk/usb_as_sata.h"
#include "nullk/vfs.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---- ABI v3 syscall wrappers ------------------------------------------- */

/* Raw syscall; also declared in nullk/abi64.h. */
uint64_t nk64_syscall(uint64_t n, uint64_t a0, uint64_t a1, uint64_t a2,
                      uint64_t a3, uint64_t a4, uint64_t a5);

/* fd 0 = UART input, fd 1 = UART output, other handles come from nk64_open. */
int64_t nk64_read(uint32_t fd, void *buf, uint32_t len);
int64_t nk64_write(uint32_t fd, const void *buf, uint32_t len);

/* A zero handle indicates failure; a stat/readdir status of 0 is success. */
uint64_t nk64_open(const char *path, uint32_t flags);
uint32_t nk64_close(uint64_t handle);
uint32_t nk64_stat(const char *path, nk_vfs_stat_t *out);
uint64_t nk64_readdir(const char *path, nk_vfs_dirent_t *out, uint32_t max);

uint32_t nk64_yield(void);
uint32_t nk64_sleep(uint32_t ticks);
uint64_t nk64_ticks(void);
uint32_t nk64_getinfo(nk64_info_t *out);
void nk64_exit(uint32_t code) __attribute__((noreturn));

uint32_t nk64_exec(const char *path);
uint32_t nk64_exec_arg(const char *path, uint64_t arg);
/* Exec the exact verified bytes; used by SecureKernel after policy checks. */
uint32_t nk64_exec_mem(const void *image, uint32_t size);
void *nk64_mmap(uint64_t hint, uint64_t len, uint32_t flags);
uint32_t nk64_log(uint32_t level, const char *text, uint32_t len);

/* Graphics/input primitives. FB_MAP succeeds for one task per boot. */
uint32_t nk64_fb_info(nk64_fbinfo_t *out);
uint64_t nk64_fb_map(void);
uint32_t nk64_input_read(nk64_input_event_t *out, uint32_t max);

uint32_t nk64_usb_read(uint32_t lun, uint64_t lba, uint32_t count, void *buf);
uint32_t nk64_usb_write(uint32_t lun, uint64_t lba, uint32_t count,
                        const void *buf);
uint32_t nk64_usb_register(const void *base, uint32_t lun_count,
                           uint32_t block_size, uint32_t blocks_per_lun);

/* A zero task id indicates failure. Entries take no argument in this port. */
uint64_t nk64_task_create(const char *name, void (*entry)(void), uint64_t arg,
                          uint32_t flags);
uint32_t nk64_task_state(uint32_t id);

/* ---- Pure helpers ------------------------------------------------------ */

size_t nk_strlen(const char *s);
char *nk_strcpy(char *dst, const char *src);
int nk_strcmp(const char *a, const char *b);
void *nk_memset(void *dst, int value, size_t n);
void *nk_memcpy(void *dst, const void *src, size_t n);
int nk_memcmp(const void *a, const void *b, size_t n);

/* Bounded converters; always NUL-terminate when cap > 0. */
char *nk_utoa_hex(uint64_t value, char *buf, uint32_t cap);
char *nk_utoa_dec(uint64_t value, char *buf, uint32_t cap);

/* Bounded formatter (%s/%d/%u/%x/%% with optional 0-padding), fd 1. */
void nk_printf(const char *fmt, ...);

/* ---- Service entry points linked into the single init ELF -------------- */

void interfacekit_server_entry(void);
/* LVGL desktop task: claims the framebuffer and pumps LVGL (see apps/lvgl64). */
void lvgl_desktop_entry(void);
void interfacekit_demo_entry(void);
void usb_service_entry(void);
void shell_service_entry(void);

/* ---- USB-as-SATA RAM-disk backend (usb64/main.c) ----------------------- */

#define NK_USB64_LUN_COUNT 2u
#define NK_USB64_LUN_BYTES 4096u
#define NK_USB64_BLOCK_SIZE 512u

int nk_ramdisk_read(void *ctx, uint32_t lun, uint64_t lba, uint32_t count,
                    void *buffer);
int nk_ramdisk_write(void *ctx, uint32_t lun, uint64_t lba, uint32_t count,
                     const void *buffer);
int nk_ramdisk_info(void *ctx, uint32_t lun, nk_uas_lun_info_t *out);

/* Short aliases used by host tests. */
int usb64_ramdisk_read(void *ctx, uint32_t lun, uint64_t lba, uint32_t count,
                       void *buffer);
int usb64_ramdisk_write(void *ctx, uint32_t lun, uint64_t lba, uint32_t count,
                        const void *buffer);

#ifdef __cplusplus
}
#endif

#endif /* NULLK_LIBNK64_H */
