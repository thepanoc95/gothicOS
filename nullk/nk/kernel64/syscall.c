/*
 * Tiny AMD64 kernel core - ABI v3 syscall dispatcher.
 *
 * Ring 0 validates every user pointer with nk64_check_user_ptr() and copies
 * through bounded helpers; no user pointer is ever dereferenced unchecked.
 * Filesystem calls are backed by the in-kernel VFS mirror of the initrd, and
 * block device calls are forwarded to a weak USB bridge backend which the
 * userspace USB service may replace at link time / through its IPC path.
 *
 * All returns use nk_status_t values (abi.h), except READ/WRITE which return
 * byte counts, TICKS/GETINFO-style queries as documented in abi64.h.
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
#include "nullk/usb_as_sata.h"

#define NK64_SYSCALL_USER_CAP 4096u
#define NK64_MAX_HANDLES      16u
#define NK64_FD_BASE          2u
#define NK64_MAX_READDIR      8u

/* From main.c: shared 512 KiB ELF/UART bounce buffer. */
extern uint8_t nk64_elf_buffer[0x80000u];

/* From elfload.c / task.c. */
int nk64_check_user_ptr(uint64_t vaddr, uint64_t len);
int nk64_task_current(void);
uint64_t nk64_task_current_cr3(void);
void nk64_task_exit_current(void);
int nk64_task_create_user(void (*entry)(void), const char *name);

/*
 * USB backend registration. The tiny kernel does not own USB storage; it only
 * keeps a descriptor for the userspace USB-as-SATA service and validates every
 * block request that the shell forwards through syscalls.
 */
typedef struct {
    uint32_t luns;
    uint32_t block_size;
    uint32_t blocks_per_lun;
    uint32_t registered;
} nk64_usb_backend_t;

#define NK64_USB_MIRROR_MAX 0x10000u
static nk64_usb_backend_t g_usb_backend;
static uint8_t g_usb_mirror[NK64_USB_MIRROR_MAX];

/* The framebuffer has exactly one claiming userspace task (the UI server). */
static uint8_t g_fb_claimed;

/* The kernel fallback shell has no userspace backend; report unsupported. */
__attribute__((weak)) int nk64_usb_backend_read(uint32_t lun, uint64_t lba,
                                                uint32_t count, void *buf)
{
    (void)lun;
    (void)lba;
    (void)count;
    (void)buf;
    return (int)NK_STATUS_UNSUPPORTED;
}

typedef struct {
    uint32_t used;
    uint32_t flags;
    uint32_t offset;
    char path[NK_VFS_PATH_MAX];
} nk64_handle_entry_t;

static nk64_handle_entry_t g_handles[NK64_MAX_HANDLES];

/* ------------------------------------------------------------------ */
/* Bounded user-memory helpers.                                        */
/* ------------------------------------------------------------------ */

static int nk64_copy_out(uint64_t dst, const void *src, uint32_t len)
{
    const uint8_t *s = (const uint8_t *)src;
    uint8_t *d;
    uint32_t i;

    if (!nk64_check_user_ptr(dst, (uint64_t)len)) {
        return 0;
    }
    d = (uint8_t *)(uintptr_t)dst;
    for (i = 0u; i < len; i++) {
        d[i] = s[i];
    }
    return 1;
}

static int nk64_copy_in(void *dst, uint64_t src, uint32_t len)
{
    uint8_t *d = (uint8_t *)dst;
    const uint8_t *s;
    uint32_t i;

    if (!nk64_check_user_ptr(src, (uint64_t)len)) {
        return 0;
    }
    s = (const uint8_t *)(uintptr_t)src;
    for (i = 0u; i < len; i++) {
        d[i] = s[i];
    }
    return 1;
}

/* Copy a NUL-terminated user string; returns 0 on bad pointer/no NUL. */
static int nk64_user_string(uint64_t src, char *dst, uint32_t cap)
{
    uint32_t i;

    if (cap == 0u) {
        return 0;
    }
    for (i = 0u; i < cap; i++) {
        uint64_t addr = src + (uint64_t)i;
        uint8_t c;

        if (!nk64_check_user_ptr(addr, 1u)) {
            return 0;
        }
        c = *(const uint8_t *)(uintptr_t)addr;
        dst[i] = (char)c;
        if (c == 0u) {
            return 1;
        }
    }
    dst[cap - 1u] = '\0';
    return 0;
}

static uint32_t nk64_count_tasks(void)
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

static int nk64_handle_index(uint64_t fd)
{
    uint64_t idx;

    if (fd < (uint64_t)NK64_FD_BASE) {
        return -1;
    }
    idx = fd - (uint64_t)NK64_FD_BASE;
    if (idx >= (uint64_t)NK64_MAX_HANDLES) {
        return -1;
    }
    if (g_handles[idx].used == 0u) {
        return -1;
    }
    return (int)idx;
}

/* ------------------------------------------------------------------ */
/* Syscall dispatcher.                                                 */
/* ------------------------------------------------------------------ */

uint64_t nk64_dispatch_syscall(uint64_t n, uint64_t a0, uint64_t a1,
                               uint64_t a2, uint64_t a3, uint64_t a4,
                               uint64_t a5)
{
    (void)a4;
    (void)a5;

    switch (n) {
    case NK64_SC_READ: {
        uint32_t len = (a2 > (uint64_t)NK64_SYSCALL_USER_CAP)
                           ? NK64_SYSCALL_USER_CAP : (uint32_t)a2;
        uint32_t i;
        uint32_t got = 0u;

        if (!nk64_check_user_ptr(a1, (uint64_t)len)) {
            return 0u;
        }
        if (a0 == 0u) {
            for (i = 0u; i < len; i++) {
                int c = nk64_uart_getc();

                if (c < 0) {
                    break;
                }
                ((uint8_t *)(uintptr_t)a1)[i] = (uint8_t)c;
                got++;
            }
            return (uint64_t)got;
        } else {
            int slot = nk64_handle_index(a0);

            if (slot < 0 || (g_handles[slot].flags & NK64_OPEN_READ) == 0u) {
                return 0u;
            }
            if (nk_vfs_read_file(g_handles[slot].path,
                                 g_handles[slot].offset,
                                 (void *)(uintptr_t)a1, len,
                                 &got) != NK_STATUS_OK) {
                return 0u;
            }
            g_handles[slot].offset += got;
            return (uint64_t)got;
        }
    }

    case NK64_SC_WRITE: {
        uint32_t len = (a2 > (uint64_t)NK64_SYSCALL_USER_CAP)
                           ? NK64_SYSCALL_USER_CAP : (uint32_t)a2;

        if (!nk64_check_user_ptr(a1, (uint64_t)len)) {
            return 0u;
        }
        if (a0 == 1u) {
            nk64_uart_puts((const char *)(uintptr_t)a1, len);
            return (uint64_t)len;
        } else {
            int slot = nk64_handle_index(a0);

            if (slot < 0 || (g_handles[slot].flags & NK64_OPEN_WRITE) == 0u) {
                return 0u;
            }
            if (nk_vfs_append_file(g_handles[slot].path,
                                   (const void *)(uintptr_t)a1,
                                   len) != NK_STATUS_OK) {
                return 0u;
            }
            return (uint64_t)len;
        }
    }

    case NK64_SC_OPEN: {
        char raw[NK_VFS_PATH_MAX];
        char path[NK_VFS_PATH_MAX];
        nk_vfs_stat_t st;
        uint32_t i;

        if (!nk64_user_string(a0, raw, (uint32_t)sizeof(raw))) {
            return 0u;   /* libnk64 treats a zero handle as failure */
        }
        if ((a1 & ((uint64_t)NK64_OPEN_READ | (uint64_t)NK64_OPEN_WRITE)) == 0u ||
            nk_vfs_normalize(raw, path, (uint32_t)sizeof(path)) != NK_STATUS_OK ||
            nk_vfs_stat(path, &st) != NK_STATUS_OK) {
            return 0u;
        }
        for (i = 0u; i < NK64_MAX_HANDLES; i++) {
            if (g_handles[i].used == 0u) {
                uint32_t j;

                g_handles[i].used = 1u;
                g_handles[i].flags = (uint32_t)a1;
                g_handles[i].offset = 0u;
                for (j = 0u; j < (uint32_t)sizeof(path); j++) {
                    g_handles[i].path[j] = path[j];
                }
                return (uint64_t)(NK64_FD_BASE + i);
            }
        }
        return 0u;
    }

    case NK64_SC_CLOSE: {
        int slot = nk64_handle_index(a0);

        if (slot < 0) {
            return (uint64_t)NK_STATUS_INVALID_ARGUMENT;
        }
        g_handles[slot].used = 0u;
        g_handles[slot].offset = 0u;
        return (uint64_t)NK_STATUS_OK;
    }

    case NK64_SC_STAT: {
        char raw[NK_VFS_PATH_MAX];
        char path[NK_VFS_PATH_MAX];
        nk_vfs_stat_t st;

        if (!nk64_user_string(a0, raw, (uint32_t)sizeof(raw)) ||
            nk_vfs_normalize(raw, path, (uint32_t)sizeof(path)) != NK_STATUS_OK ||
            nk_vfs_stat(path, &st) != NK_STATUS_OK) {
            return (uint64_t)NK_STATUS_NOT_FOUND;
        }
        if (!nk64_copy_out(a1, &st, (uint32_t)sizeof(st))) {
            return (uint64_t)NK_STATUS_INVALID_ARGUMENT;
        }
        return (uint64_t)NK_STATUS_OK;
    }

    case NK64_SC_READDIR: {
        char raw[NK_VFS_PATH_MAX];
        char path[NK_VFS_PATH_MAX];
        nk_vfs_dirent_t ents[NK64_MAX_READDIR];
        uint32_t max = (a2 > (uint64_t)NK64_MAX_READDIR)
                           ? NK64_MAX_READDIR : (uint32_t)a2;
        uint32_t count = 0u;
        uint32_t i;

        if (!nk64_user_string(a0, raw, (uint32_t)sizeof(raw)) ||
            nk_vfs_normalize(raw, path, (uint32_t)sizeof(path)) != NK_STATUS_OK) {
            return 0u;
        }
        if (nk_vfs_readdir(path, ents, max, &count) != NK_STATUS_OK) {
            return 0u;
        }
        for (i = 0u; i < count; i++) {
            if (!nk64_copy_out(a1 + (uint64_t)i * (uint64_t)sizeof(ents[0]),
                               &ents[i], (uint32_t)sizeof(ents[0]))) {
                return 0u;
            }
        }
        return (uint64_t)count;
    }

    case NK64_SC_YIELD:
        nk64_task_yield();
        return (uint64_t)NK_STATUS_OK;

    case NK64_SC_SLEEP: {
        uint32_t target = nk64_ticks + (uint32_t)a0;

        /* SYSCALL clears IF on entry; HLT with interrupts masked would never
         * observe the PIT. Re-enable IRQs and let other tasks run while we
         * wait for the target tick. */
        nk64_irq_enable();
        while ((uint32_t)nk64_ticks < target) {
            nk64_task_yield();
        }
        return (uint64_t)NK_STATUS_OK;
    }

    case NK64_SC_TICKS:
        return (uint64_t)nk64_ticks;

    case NK64_SC_GETINFO: {
        nk64_info_t info;

        info.abi_version = NULLK64_ABI_VERSION;
        info.magic = NULLK64_ABI_MAGIC;
        info.runlevel = nk64_runlevel;
        info.tick_hz = 100u;
        info.ticks = nk64_ticks;
        info.task_count = nk64_count_tasks();
        info.flags = (uint32_t)NK_FEATURE_TASKS | (uint32_t)NK_FEATURE_MEMORY |
                     (uint32_t)NK_FEATURE_PREEMPT;
        info.hhdm_offset = nk64_boot.hhdm;
        info.mem_total = nk64_boot.mem_total;
        if (!nk64_copy_out(a0, &info, (uint32_t)sizeof(info))) {
            return (uint64_t)NK_STATUS_INVALID_ARGUMENT;
        }
        return (uint64_t)NK_STATUS_OK;
    }

    case NK64_SC_EXIT:
        nk64_task_exit_current();
        return (uint64_t)NK_STATUS_OK;   /* unreachable */

    case NK64_SC_EXEC: {
        char raw[NK_VFS_PATH_MAX];
        char path[NK_VFS_PATH_MAX];
        uint32_t len = 0u;

        if (!nk64_user_string(a0, raw, (uint32_t)sizeof(raw)) ||
            nk_vfs_normalize(raw, path, (uint32_t)sizeof(path)) != NK_STATUS_OK) {
            return (uint64_t)NK_STATUS_INVALID_ARGUMENT;
        }
        if (nk_vfs_read_file(path, 0u, nk64_elf_buffer, 0x80000u,
                             &len) != NK_STATUS_OK || len == 0u) {
            return (uint64_t)NK_STATUS_NOT_FOUND;
        }
        if (nk64_task_create_elf(nk64_elf_buffer, len, "exec") < 0) {
            return (uint64_t)NK_STATUS_INTERNAL_ERROR;
        }
        (void)a1;   /* argument string is not forwarded yet */
        return (uint64_t)NK_STATUS_OK;
    }

    case NK64_SC_EXEC_MEM: {
        /* SecureKernel stages and verifies an image in userspace, then execs
         * these exact bytes. No second VFS read means no check/use window. */
        if (a1 == 0u || a1 > (uint64_t)sizeof(nk64_elf_buffer) ||
            !nk64_copy_in(nk64_elf_buffer, a0, (uint32_t)a1)) {
            return (uint64_t)NK_STATUS_INVALID_ARGUMENT;
        }
        if (nk64_task_create_elf(nk64_elf_buffer, (uint32_t)a1, "exec") < 0) {
            return (uint64_t)NK_STATUS_INTERNAL_ERROR;
        }
        return (uint64_t)NK_STATUS_OK;
    }

    case NK64_SC_FB_INFO: {
        if (nk64_boot.fb.present == 0u ||
            !nk64_copy_out(a0, &nk64_boot.fb,
                           (uint32_t)sizeof(nk64_fbinfo_t))) {
            return (uint64_t)NK_STATUS_NOT_FOUND;
        }
        return (uint64_t)NK_STATUS_OK;
    }

    case NK64_SC_FB_MAP: {
        uint64_t bytes;
        uint64_t cr3;

        if (nk64_boot.fb.present == 0u) {
            return (uint64_t)NK_STATUS_NOT_FOUND;
        }
        if (g_fb_claimed != 0u) {
            return (uint64_t)NK_STATUS_PERMISSION_DENIED;
        }
        bytes = (uint64_t)nk64_boot.fb.pitch * nk64_boot.fb.height;
        cr3 = nk64_task_current_cr3();
        if (bytes == 0u || bytes > (64u * 1024u * 1024u) || cr3 == 0u) {
            return (uint64_t)NK_STATUS_INTERNAL_ERROR;
        }
        /* The display scans these bytes while ring 3 writes them. A cached
         * alias can leave scanout with stale or partially evicted pixels; use
         * a strong uncacheable user mapping for the claimed framebuffer. */
        nk64_vmm_map(cr3, (uint64_t)NK64_FB_VBASE, nk64_boot.fb.phys, bytes,
                     NK64_PAGE_USER_UC | NK64_PAGE_NX);
        g_fb_claimed = 1u;
        return (uint64_t)NK64_FB_VBASE;
    }

    case NK64_SC_INPUT_READ: {
        nk64_input_event_t events[16];
        uint32_t max = (a1 > 16u) ? 16u : (uint32_t)a1;
        uint32_t count;

        if (max == 0u) {
            return 0u;
        }
        count = nk64_input_read(events, max);
        if (count == 0u) {
            return 0u;
        }
        if (!nk64_copy_out(a0, events,
                           count * (uint32_t)sizeof(nk64_input_event_t))) {
            return 0u;
        }
        return (uint64_t)count;
    }

    case NK64_SC_MMAP: {
        uint64_t base = (a0 != 0u) ? a0 : (uint64_t)NK64_USER_HEAP;
        uint64_t len = a1;

        if ((a2 & (uint64_t)NK64_MM_SANDBOX) != 0u) {
            /* Every user task already has a 64 KiB sandbox window mapped;
             * grow it if the caller asked for more (bounded by the global
             * sandbox size). */
            uint64_t want = (len != 0u) ? len : (uint64_t)NK64_SANDBOX_SIZE;
            if (want > (uint64_t)NK64_SANDBOX_SIZE) {
                want = (uint64_t)NK64_SANDBOX_SIZE;
            }
            (void)nk64_vmm_alloc_user_pages(nk64_task_current_cr3(),
                                            (uint64_t)NK64_SANDBOX_BASE,
                                            want);
            return (uint64_t)NK64_SANDBOX_BASE;
        }
        if (len == 0u || len > 0x100000u ||
            !nk64_check_user_ptr(base, len)) {
            return 0u;
        }
        if (nk64_vmm_alloc_user_pages(nk64_task_current_cr3(), base,
                                      len) == 0u) {
            return 0u;
        }
        return base;
    }

    case NK64_SC_IRQ_BIND:
    case NK64_SC_IPC_OPEN:
    case NK64_SC_IPC_SEND:
    case NK64_SC_IPC_RECV:
    case NK64_SC_IPC_REPLY:
        /* IPC/IRQ endpoints are userspace services in the reverse-monolithic
         * design; the tiny core does not implement them. */
        return (uint64_t)NK_STATUS_UNSUPPORTED;

    case NK64_SC_LOG: {
        char text[256];
        uint32_t len = (a2 > (uint64_t)(sizeof(text) - 1u))
                           ? (uint32_t)(sizeof(text) - 1u) : (uint32_t)a2;

        if (a0 >= (uint64_t)NK_KERN_LEVEL_COUNT ||
            !nk64_check_user_ptr(a1, (uint64_t)len)) {
            return (uint64_t)NK_STATUS_INVALID_ARGUMENT;
        }
        if (!nk64_copy_in(text, a1, len)) {
            return (uint64_t)NK_STATUS_INVALID_ARGUMENT;
        }
        text[len] = '\0';
        nk_printk((int)a0, "%s", text);
        return (uint64_t)NK_STATUS_OK;
    }

    case NK64_SC_REBOOT:
        nk64_reboot();
        return (uint64_t)NK_STATUS_OK;   /* unreachable */

    case NK64_SC_TASK_CREATE: {
        char name[NK64_NAME_MAX];
        int id;

        if (!nk64_user_string(a0, name, (uint32_t)sizeof(name))) {
            return 0u;
        }
        /* ABI v3 entries are ring-3 functions inside the caller's address
         * space, not privileged kernel threads. */
        id = nk64_task_create_user((void (*)(void))(uintptr_t)a1, name);
        (void)a3;   /* arg/flags: entries take no arguments in this port */
        /* libnk64 treats a zero task id as failure. */
        return (id >= 0) ? (uint64_t)id : 0u;
    }

    case NK64_SC_TASK_STATE: {
        int id = (a0 > 0x7fffffffULL) ? -1 : (int)a0;

        return (uint64_t)(nk64_task_alive(id) ? NK_TASK_RUNNING
                                              : NK_TASK_DEAD);
    }

    case NK64_SC_USB_REGISTER: {
        uint64_t size;

        if (a1 == 0u || a1 > 16u || a2 == 0u || a3 == 0u ||
            (a2 & (a2 - 1u)) != 0u) {
            return (uint64_t)NK_STATUS_INVALID_ARGUMENT;
        }
        size = a1 * a2 * a3;
        if (size == 0u || size > (uint64_t)sizeof(g_usb_mirror) ||
            !nk64_copy_in(g_usb_mirror, a0, (uint32_t)size)) {
            return (uint64_t)NK_STATUS_INVALID_ARGUMENT;
        }
        g_usb_backend.luns = (uint32_t)a1;
        g_usb_backend.block_size = (uint32_t)a2;
        g_usb_backend.blocks_per_lun = (uint32_t)a3;
        g_usb_backend.registered = 1u;
        return (uint64_t)NK_STATUS_OK;
    }

    case NK64_SC_USB_READ:
    case NK64_SC_USB_WRITE: {
        uint32_t lun = (uint32_t)a0;
        uint64_t lba = a1;
        uint32_t count = (uint32_t)a2;
        uint64_t offset;
        uint64_t bytes;
        uint64_t disk_size;

        if (g_usb_backend.registered == 0u || lun >= g_usb_backend.luns ||
            count == 0u || lba >= g_usb_backend.blocks_per_lun ||
            count > g_usb_backend.blocks_per_lun - (uint32_t)lba) {
            return (uint64_t)NK_STATUS_INVALID_ARGUMENT;
        }
        bytes = (uint64_t)count * g_usb_backend.block_size;
        if (!nk64_check_user_ptr(a3, bytes)) {
            return (uint64_t)NK_STATUS_INVALID_ARGUMENT;
        }
        offset = (lun * g_usb_backend.blocks_per_lun + lba) *
                 g_usb_backend.block_size;
        disk_size = g_usb_backend.luns * g_usb_backend.blocks_per_lun *
                    g_usb_backend.block_size;
        if (offset > disk_size || bytes > disk_size - offset) {
            return (uint64_t)NK_STATUS_INVALID_ARGUMENT;
        }
        if (n == NK64_SC_USB_READ) {
            nk64_copy_out(a3, g_usb_mirror + offset, (uint32_t)bytes);
        } else {
            nk64_copy_in(g_usb_mirror + offset, a3, (uint32_t)bytes);
        }
        return (uint64_t)NK_STATUS_OK;
    }

    default:
        return (uint64_t)NK_STATUS_UNSUPPORTED;
    }
}
