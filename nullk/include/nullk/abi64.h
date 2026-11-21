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
    NK64_SC_EXEC_MEM = 25,   /* image, size -> status (verified buffer) */
    NK64_SC_FB_INFO = 26,    /* fbinfo_out -> status */
    NK64_SC_FB_MAP = 27,     /* -> userspace address of the claimed framebuffer */
    NK64_SC_INPUT_READ = 28, /* events_out, max -> event count */
    NK64_SC_COUNT = 29
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

/*
 * Graphics and input are bounded kernel primitives: ring 0 only reports the
 * Limine framebuffer and hands out one mapping to a claiming userspace task.
 * Drawing, window management and input routing belong to the UI server.
 */
typedef struct {
    uint64_t phys;              /* physical framebuffer base */
    uint32_t width;
    uint32_t height;
    uint32_t pitch;             /* bytes per scanline */
    uint16_t bpp;               /* bits per pixel (32 or 24 in practice) */
    uint8_t red_size;
    uint8_t red_shift;
    uint8_t green_size;
    uint8_t green_shift;
    uint8_t blue_size;
    uint8_t blue_shift;
    uint8_t present;            /* 1 when Limine supplied a usable display */
    uint8_t reserved[3];
} nk64_fbinfo_t;

enum {
    NK64_INPUT_KEY_DOWN = 1u,
    NK64_INPUT_KEY_UP = 2u,
    NK64_INPUT_MOUSE_MOVE = 3u,
    NK64_INPUT_MOUSE_DOWN = 4u,
    NK64_INPUT_MOUSE_UP = 5u
};

enum {
    NK64_MOD_SHIFT = 1u,
    NK64_MOD_CTRL = 2u,
    NK64_MOD_ALT = 4u
};

/* Printable keys carry their ASCII value; specials use the range below. */
#define NK64_KEY_NONE     0u
#define NK64_KEY_UP       0x101u
#define NK64_KEY_DOWN     0x102u
#define NK64_KEY_LEFT     0x103u
#define NK64_KEY_RIGHT    0x104u
#define NK64_KEY_HOME     0x105u
#define NK64_KEY_END      0x106u
#define NK64_KEY_PAGEUP   0x107u
#define NK64_KEY_PAGEDOWN 0x108u
#define NK64_KEY_INSERT   0x109u
#define NK64_KEY_DELETE   0x10au
#define NK64_KEY_BACKSPACE 0x08u
#define NK64_KEY_TAB      0x09u
#define NK64_KEY_ENTER    0x0au
#define NK64_KEY_ESCAPE   0x1bu
#define NK64_KEY_LSHIFT   0x110u
#define NK64_KEY_RSHIFT   0x111u
#define NK64_KEY_LCTRL    0x112u
#define NK64_KEY_RCTRL    0x113u
#define NK64_KEY_LALT     0x114u
#define NK64_KEY_RALT     0x115u
#define NK64_KEY_CAPSLOCK 0x116u
#define NK64_MOUSE_LEFT   1u
#define NK64_MOUSE_RIGHT  2u
#define NK64_MOUSE_MIDDLE 4u

typedef struct {
    uint32_t type;              /* NK64_INPUT_* */
    uint32_t code;              /* ASCII or NK64_KEY_* */
    uint32_t modifiers;         /* NK64_MOD_* */
    int32_t x;                  /* absolute screen coordinates */
    int32_t y;
    int32_t dx;                 /* mouse delta since previous packet */
    int32_t dy;
    uint32_t buttons;           /* NK64_MOUSE_* */
    uint32_t reserved;
} nk64_input_event_t;

/* Pure PS/2 decoders shared by the kernel driver and the host tests. */
uint32_t nk64_scancode_key(uint8_t scancode, uint32_t extended);
int nk64_mouse_packet(const uint8_t bytes[3], int32_t *dx, int32_t *dy,
                      uint32_t *buttons);

/* Userspace syscall shim declarations (implemented in libnk64). */
uint64_t nk64_syscall(uint64_t n, uint64_t a0, uint64_t a1, uint64_t a2,
                      uint64_t a3, uint64_t a4, uint64_t a5);

#endif /* NULLK_ABI64_H */
