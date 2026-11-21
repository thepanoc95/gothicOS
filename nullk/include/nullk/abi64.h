/*
 * NULLK ABI v3 - AMD64 syscall contract.
 *
 * Reverse-monolithic rule: ring 0 owns only CPU, memory, interrupts, IPC and
 * scheduling. Filesystems, USB/SATA, loading and shells are userspace tasks.
 *
 * x86-64 syscall convention:
 *   rax = syscall number, args = rdi, rsi, rdx, r10, r8, r9
 *   return = rax (negative errno-style values are never used; see abi.h)
 */
#ifndef NULLK_ABI64_H
#define NULLK_ABI64_H

#include <stdint.h>

#define NULLK64_ABI_VERSION 0x00030000u
#define NULLK64_ABI_MAGIC   0x4E4B5633u  /* "NKV3" */

#define NK64_NAME_MAX 32u
#define NK64_PATH_MAX 192u
#define NK64_MAX_TASKS 32u
#define NK64_REQUEST_MAX 64u

enum {
    NK64_SC_READ = 0,        /* fd, buf, len -> bytes (fd 0 = UART) */
    NK64_SC_WRITE = 1,       /* fd, buf, len -> bytes (fd 1 = UART) */
    NK64_SC_OPEN = 2,        /* path, flags -> handle */
    NK64_SC_CLOSE = 3,       /* handle -> status */
    NK64_SC_STAT = 4,        /* path, stat_out -> status */
    NK64_SC_READDIR = 5,     /* path, dirent_out, max -> count */
    NK64_SC_YIELD = 6,
    NK64_SC_SLEEP = 7,       /* ticks */
    NK64_SC_TICKS = 8,
    NK64_SC_GETINFO = 9,     /* info_out -> status */
    NK64_SC_EXIT = 10,
    NK64_SC_EXEC = 11,       /* path, arg -> status */
    NK64_SC_MMAP = 12,       /* addr_hint, len, flags -> vaddr */
    NK64_SC_IRQ_BIND = 13,   /* irq, endpoint */
    NK64_SC_IPC_OPEN = 14,
    NK64_SC_IPC_SEND = 15,
    NK64_SC_IPC_RECV = 16,
    NK64_SC_IPC_REPLY = 17,
    NK64_SC_LOG = 18,        /* level, text, len */
    NK64_SC_REBOOT = 19,
    NK64_SC_TASK_CREATE = 20,/* name, entry, arg, flags -> id */
    NK64_SC_TASK_STATE = 21, /* id -> state */
    NK64_SC_USB_READ = 22,   /* lun, lba, count, buf -> status */
    NK64_SC_USB_WRITE = 23,  /* lun, lba, count, buf -> status */
    NK64_SC_USB_REGISTER = 24, /* base, luns, blk_size, blocks -> status */
    NK64_SC_COUNT = 25
};

enum {
    NK64_OPEN_READ = 1u,
    NK64_OPEN_WRITE = 2u
};

enum {
    NK64_MM_RW = 1u,
    NK64_MM_SANDBOX = 2u,   /* returned region is the caller's safe window */
    NK64_MM_USER = 4u
};

typedef struct {
    uint32_t abi_version;
    uint32_t magic;
    uint32_t runlevel;
    uint32_t tick_hz;
    uint64_t ticks;
    uint32_t task_count;
    uint32_t flags;
    uint64_t hhdm_offset;
    uint64_t mem_total;
} nk64_info_t;

/* Userspace syscall shim declarations (implemented in libnk64). */
uint64_t nk64_syscall(uint64_t n, uint64_t a0, uint64_t a1, uint64_t a2,
                      uint64_t a3, uint64_t a4, uint64_t a5);

#endif /* NULLK_ABI64_H */
