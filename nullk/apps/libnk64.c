/*
 * libnk64 - freestanding AMD64 userspace runtime for NULLK ABI v3.
 *
 * The syscall primitive uses the x86-64 `syscall` instruction directly:
 * rcx and r11 are clobbered by the CPU, argument 4 travels in r10 (not rcx).
 * Everything else is in-task code; there is no libc and no heap here.
 */
#include <stdarg.h>
#include <stdint.h>

#include "libnk64.h"

/* ---- syscall primitive -------------------------------------------------- */

/* The freestanding AMD64 port uses the `syscall` instruction directly. Other
 * hosts (currently the Linux ABI v3 port) provide their own primitive and set
 * NULLK_LIBNK64_EXTERNAL_SYSCALL; all wrappers below stay identical. */
#ifndef NULLK_LIBNK64_EXTERNAL_SYSCALL
uint64_t nk64_syscall(uint64_t n, uint64_t a0, uint64_t a1, uint64_t a2,
                      uint64_t a3, uint64_t a4, uint64_t a5)
{
    register uint64_t rax __asm__("rax") = n;
    register uint64_t rdi __asm__("rdi") = a0;
    register uint64_t rsi __asm__("rsi") = a1;
    register uint64_t rdx __asm__("rdx") = a2;
    register uint64_t r10 __asm__("r10") = a3;
    register uint64_t r8  __asm__("r8")  = a4;
    register uint64_t r9  __asm__("r9")  = a5;

    __asm__ volatile("syscall"
                     : "+r"(rax)
                     : "r"(rdi), "r"(rsi), "r"(rdx), "r"(r10), "r"(r8),
                       "r"(r9)
                     : "rcx", "r11", "memory");
    return rax;
}
#endif

/* ---- ABI v3 wrappers --------------------------------------------------- */

int64_t nk64_read(uint32_t fd, void *buf, uint32_t len)
{
    return (int64_t)nk64_syscall(NK64_SC_READ, fd, (uint64_t)(uintptr_t)buf,
                                 len, 0, 0, 0);
}

int64_t nk64_write(uint32_t fd, const void *buf, uint32_t len)
{
    return (int64_t)nk64_syscall(NK64_SC_WRITE, fd, (uint64_t)(uintptr_t)buf,
                                 len, 0, 0, 0);
}

uint64_t nk64_open(const char *path, uint32_t flags)
{
    return nk64_syscall(NK64_SC_OPEN, (uint64_t)(uintptr_t)path, flags, 0, 0,
                        0, 0);
}

uint32_t nk64_close(uint64_t handle)
{
    return (uint32_t)nk64_syscall(NK64_SC_CLOSE, handle, 0, 0, 0, 0, 0);
}

uint32_t nk64_stat(const char *path, nk_vfs_stat_t *out)
{
    return (uint32_t)nk64_syscall(NK64_SC_STAT, (uint64_t)(uintptr_t)path,
                                  (uint64_t)(uintptr_t)out, 0, 0, 0, 0);
}

uint64_t nk64_readdir(const char *path, nk_vfs_dirent_t *out, uint32_t max)
{
    return nk64_syscall(NK64_SC_READDIR, (uint64_t)(uintptr_t)path,
                        (uint64_t)(uintptr_t)out, max, 0, 0, 0);
}

uint32_t nk64_yield(void)
{
    return (uint32_t)nk64_syscall(NK64_SC_YIELD, 0, 0, 0, 0, 0, 0);
}

uint32_t nk64_sleep(uint32_t ticks)
{
    return (uint32_t)nk64_syscall(NK64_SC_SLEEP, ticks, 0, 0, 0, 0, 0);
}

uint64_t nk64_ticks(void)
{
    return nk64_syscall(NK64_SC_TICKS, 0, 0, 0, 0, 0, 0);
}

uint32_t nk64_getinfo(nk64_info_t *out)
{
    return (uint32_t)nk64_syscall(NK64_SC_GETINFO, (uint64_t)(uintptr_t)out, 0,
                                  0, 0, 0, 0);
}

void nk64_exit(uint32_t code)
{
    (void)nk64_syscall(NK64_SC_EXIT, code, 0, 0, 0, 0, 0);
    /* EXIT should never return, but never fall through into caller code. */
    for (;;) {
        (void)nk64_syscall(NK64_SC_YIELD, 0, 0, 0, 0, 0, 0);
    }
}

uint32_t nk64_exec_arg(const char *path, uint64_t arg)
{
    return (uint32_t)nk64_syscall(NK64_SC_EXEC, (uint64_t)(uintptr_t)path, arg,
                                  0, 0, 0, 0);
}

uint32_t nk64_exec(const char *path)
{
    return nk64_exec_arg(path, 0);
}

uint32_t nk64_exec_mem(const void *image, uint32_t size)
{
    return (uint32_t)nk64_syscall(NK64_SC_EXEC_MEM,
                                  (uint64_t)(uintptr_t)image, (uint64_t)size,
                                  0, 0, 0, 0);
}

void *nk64_mmap(uint64_t hint, uint64_t len, uint32_t flags)
{
    uint64_t addr = nk64_syscall(NK64_SC_MMAP, hint, len, flags, 0, 0, 0);
    return (void *)(uintptr_t)addr;
}

uint32_t nk64_log(uint32_t level, const char *text, uint32_t len)
{
    return (uint32_t)nk64_syscall(NK64_SC_LOG, level, (uint64_t)(uintptr_t)text,
                                  len, 0, 0, 0);
}

uint32_t nk64_fb_info(nk64_fbinfo_t *out)
{
    return (uint32_t)nk64_syscall(NK64_SC_FB_INFO,
                                  (uint64_t)(uintptr_t)out, 0, 0, 0, 0, 0);
}

uint64_t nk64_fb_map(void)
{
    return nk64_syscall(NK64_SC_FB_MAP, 0, 0, 0, 0, 0, 0);
}

uint32_t nk64_input_read(nk64_input_event_t *out, uint32_t max)
{
    return (uint32_t)nk64_syscall(NK64_SC_INPUT_READ,
                                  (uint64_t)(uintptr_t)out, max, 0, 0, 0, 0);
}

uint32_t nk64_usb_read(uint32_t lun, uint64_t lba, uint32_t count, void *buf)
{
    return (uint32_t)nk64_syscall(NK64_SC_USB_READ, lun, lba, count,
                                  (uint64_t)(uintptr_t)buf, 0, 0);
}

uint32_t nk64_usb_write(uint32_t lun, uint64_t lba, uint32_t count,
                        const void *buf)
{
    return (uint32_t)nk64_syscall(NK64_SC_USB_WRITE, lun, lba, count,
                                  (uint64_t)(uintptr_t)buf, 0, 0);
}

uint32_t nk64_usb_register(const void *base, uint32_t lun_count,
                           uint32_t block_size, uint32_t blocks_per_lun)
{
    return (uint32_t)nk64_syscall(NK64_SC_USB_REGISTER,
                                  (uint64_t)(uintptr_t)base, lun_count,
                                  block_size, blocks_per_lun, 0, 0);
}

uint64_t nk64_task_create(const char *name, void (*entry)(void), uint64_t arg,
                          uint32_t flags)
{
    return nk64_syscall(NK64_SC_TASK_CREATE, (uint64_t)(uintptr_t)name,
                        (uint64_t)(uintptr_t)entry, arg, flags, 0, 0);
}

uint32_t nk64_task_state(uint32_t id)
{
    return (uint32_t)nk64_syscall(NK64_SC_TASK_STATE, id, 0, 0, 0, 0, 0);
}

/* ---- string / memory helpers ------------------------------------------ */

size_t nk_strlen(const char *s)
{
    size_t n = 0;

    while (s[n] != '\0') {
        n++;
    }
    return n;
}

char *nk_strcpy(char *dst, const char *src)
{
    char *out = dst;

    while (*src != '\0') {
        *out++ = *src++;
    }
    *out = '\0';
    return dst;
}

int nk_strcmp(const char *a, const char *b)
{
    while (*a != '\0' && *a == *b) {
        a++;
        b++;
    }
    return (int)(unsigned char)*a - (int)(unsigned char)*b;
}

void *nk_memset(void *dst, int value, size_t n)
{
    uint8_t *d = (uint8_t *)dst;

    for (size_t i = 0; i < n; i++) {
        d[i] = (uint8_t)value;
    }
    return dst;
}

void *nk_memcpy(void *dst, const void *src, size_t n)
{
    uint8_t *d = (uint8_t *)dst;
    const uint8_t *s = (const uint8_t *)src;

    if (d == s) {
        return dst;
    }
    /* Overlapping copies are not part of the libnk64 contract. */
    for (size_t i = 0; i < n; i++) {
        d[i] = s[i];
    }
    return dst;
}

int nk_memcmp(const void *a, const void *b, size_t n)
{
    const uint8_t *x = (const uint8_t *)a;
    const uint8_t *y = (const uint8_t *)b;

    for (size_t i = 0; i < n; i++) {
        if (x[i] != y[i]) {
            return (int)x[i] - (int)y[i];
        }
    }
    return 0;
}

/*
 * clang can lower struct copies in the C++ security layer to libc calls even
 * with -fno-builtin. These tiny shims keep init.elf fully freestanding; the
 * kernel image never needs them. Hosted ports must use the real libc, so the
 * same external-backend switch suppresses them there.
 */
#ifndef NULLK_LIBNK64_EXTERNAL_SYSCALL
void *memcpy(void *dst, const void *src, size_t n)
{
    return nk_memcpy(dst, src, n);
}

void *memset(void *dst, int value, size_t n)
{
    return nk_memset(dst, value, n);
}

int memcmp(const void *a, const void *b, size_t n)
{
    return nk_memcmp(a, b, n);
}

void *memmove(void *dst, const void *src, size_t n)
{
    uint8_t *d = (uint8_t *)dst;
    const uint8_t *s = (const uint8_t *)src;

    if (d == s || n == 0u) {
        return dst;
    }
    if (d > s && d < s + n) {
        for (size_t i = n; i != 0u; i--) {
            d[i - 1u] = s[i - 1u];
        }
        return dst;
    }
    return nk_memcpy(dst, src, n);
}
#endif /* !NULLK_LIBNK64_EXTERNAL_SYSCALL */

static char *nk_utoa_base(uint64_t value, char *buf, uint32_t cap,
                          uint32_t base)
{
    char tmp[21];
    uint32_t n = 0;

    if (cap == 0) {
        return buf;
    }
    if (base < 2 || base > 16) {
        base = 10;
    }
    if (value == 0) {
        buf[0] = '0';
        buf[1] = '\0';
        return buf;
    }
    while (value != 0 && n < sizeof(tmp)) {
        uint32_t digit = (uint32_t)(value % base);
        tmp[n++] = (char)(digit < 10 ? '0' + digit : 'a' + (digit - 10));
        value /= base;
    }
    if (n >= cap) {
        n = cap - 1;
    }
    for (uint32_t i = 0; i < n; i++) {
        buf[i] = tmp[n - 1 - i];
    }
    buf[n] = '\0';
    return buf;
}

char *nk_utoa_hex(uint64_t value, char *buf, uint32_t cap)
{
    return nk_utoa_base(value, buf, cap, 16);
}

char *nk_utoa_dec(uint64_t value, char *buf, uint32_t cap)
{
    return nk_utoa_base(value, buf, cap, 10);
}

/* ---- minimal formatter ------------------------------------------------- */

#define NK_PRINTF_CHUNK 256u

typedef struct {
    char buf[NK_PRINTF_CHUNK];
    uint32_t len;
} nk_printf_out_t;

static void nk_printf_flush(nk_printf_out_t *out)
{
    if (out->len != 0) {
        (void)nk64_write(1, out->buf, out->len);
        out->len = 0;
    }
}

static void nk_printf_putc(nk_printf_out_t *out, char c)
{
    if (out->len == NK_PRINTF_CHUNK) {
        nk_printf_flush(out);
    }
    out->buf[out->len++] = c;
}

static void nk_printf_puts(nk_printf_out_t *out, const char *s)
{
    if (s == NULL) {
        s = "(null)";
    }
    while (*s != '\0') {
        nk_printf_putc(out, *s++);
    }
}

void nk_printf(const char *fmt, ...)
{
    nk_printf_out_t out;
    va_list ap;
    const char *p;

    out.len = 0;
    va_start(ap, fmt);
    for (p = fmt; *p != '\0'; p++) {
        uint32_t width = 0;
        char num[24];
        char spec;

        if (*p != '%') {
            nk_printf_putc(&out, *p);
            continue;
        }
        p++;
        if (*p == '\0') {
            break;
        }
        if (*p == '%') {
            nk_printf_putc(&out, '%');
            continue;
        }
        if (*p == '0') {
            p++;
            while (*p >= '0' && *p <= '9') {
                width = width * 10u + (uint32_t)(*p - '0');
                p++;
            }
        }
        if (*p == '\0') {
            break;
        }
        spec = *p;

        switch (spec) {
        case 's':
            nk_printf_puts(&out, va_arg(ap, const char *));
            break;
        case 'd': {
            int v = va_arg(ap, int);
            uint32_t u;
            if (v < 0) {
                nk_printf_putc(&out, '-');
                u = (uint32_t)(-(int64_t)v);
            } else {
                u = (uint32_t)v;
            }
            nk_utoa_dec(u, num, sizeof(num));
            goto emit_number;
        }
        case 'u':
            nk_utoa_dec(va_arg(ap, uint32_t), num, sizeof(num));
            goto emit_number;
        case 'x':
            nk_utoa_hex(va_arg(ap, uint32_t), num, sizeof(num));
            goto emit_number;
        default:
            nk_printf_putc(&out, '%');
            nk_printf_putc(&out, spec);
            break;
        }
        continue;

    emit_number:
        while (width > nk_strlen(num)) {
            nk_printf_putc(&out, '0');
            width--;
        }
        nk_printf_puts(&out, num);
    }
    va_end(ap);
    nk_printf_flush(&out);
}
