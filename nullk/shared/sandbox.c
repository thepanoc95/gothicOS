/*
 * null_kernel memory sandbox.
 *
 * Every access is checked against a fixed [base, base+size) window using
 * subtraction (never base+len) so overflowing addresses are rejected safely.
 */
#include "nullk/sandbox.h"
#include "nullk/abi.h"

#include <stddef.h>

static void nk_memcpy(void *dst, const void *src, uint32_t n)
{
    uint8_t *d = (uint8_t *)dst;
    const uint8_t *s = (const uint8_t *)src;
    uint32_t i;

    for (i = 0u; i < n; i++) {
        d[i] = s[i];
    }
}

int nk_sandbox_init(nk_sandbox_t *s, uint64_t base, uint64_t size,
                    uint32_t flags)
{
    if (s == (nk_sandbox_t *)0) {
        return NK_STATUS_INVALID_ARGUMENT;
    }
    if ((size != 0u) && (base > (UINT64_MAX - size))) {
        return NK_STATUS_INVALID_ARGUMENT;
    }
    s->base = base;
    s->size = size;
    s->flags = flags;
    return NK_STATUS_OK;
}

int nk_sandbox_contains(const nk_sandbox_t *s, uint64_t addr, uint32_t len)
{
    if (s == (const nk_sandbox_t *)0) {
        return 0;
    }
    if (len > s->size) {
        return 0;
    }
    if (addr < s->base) {
        return 0;
    }
    /* Overflow-safe: equivalent to (addr + len) <= (base + size). */
    if ((addr - s->base) > (s->size - (uint64_t)len)) {
        return 0;
    }
    return 1;
}

int nk_sandbox_read(const nk_sandbox_t *s, uint64_t addr, void *buf,
                    uint32_t len)
{
    if ((s == (const nk_sandbox_t *)0) ||
        ((buf == (void *)0) && (len != 0u))) {
        return NK_STATUS_INVALID_ARGUMENT;
    }
    if (!nk_sandbox_contains(s, addr, len)) {
        return NK_STATUS_OUT_OF_MEMORY;
    }
    if (len != 0u) {
        nk_memcpy(buf, (const void *)(uintptr_t)addr, len);
    }
    return NK_STATUS_OK;
}

int nk_sandbox_write(const nk_sandbox_t *s, uint64_t addr, const void *buf,
                     uint32_t len)
{
    if ((s == (const nk_sandbox_t *)0) ||
        ((buf == (const void *)0) && (len != 0u))) {
        return NK_STATUS_INVALID_ARGUMENT;
    }
    if ((s->flags & NK_SANDBOX_READONLY) != 0u) {
        return NK_STATUS_PERMISSION_DENIED;
    }
    if (!nk_sandbox_contains(s, addr, len)) {
        return NK_STATUS_OUT_OF_MEMORY;
    }
    if (len != 0u) {
        nk_memcpy((void *)(uintptr_t)addr, buf, len);
    }
    return NK_STATUS_OK;
}
