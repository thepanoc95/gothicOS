/*
 * System memory sandbox: the only memory the nk) shell is allowed to read or
 * write. Bounds are checked on every access; privileged callers can grant a
 * narrower window than physical memory.
 */
#ifndef NULLK_SANDBOX_H
#define NULLK_SANDBOX_H

#include <stdint.h>

enum {
    NK_SANDBOX_READONLY = 1u << 0,
    NK_SANDBOX_KERNEL = 1u << 1
};

typedef struct {
    uint64_t base;
    uint64_t size;
    uint32_t flags;
} nk_sandbox_t;

int nk_sandbox_init(nk_sandbox_t *s, uint64_t base, uint64_t size,
                    uint32_t flags);
int nk_sandbox_contains(const nk_sandbox_t *s, uint64_t addr, uint32_t len);
int nk_sandbox_read(const nk_sandbox_t *s, uint64_t addr, void *buf,
                    uint32_t len);
int nk_sandbox_write(const nk_sandbox_t *s, uint64_t addr, const void *buf,
                     uint32_t len);

#endif /* NULLK_SANDBOX_H */
