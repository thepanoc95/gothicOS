/*
 * SecureKernel userspace service entry.
 *
 * The layer itself is a C++ library linked into ring 3 tasks. This small
 * service only announces that the integrity policy is active; exec callers use
 * the C ABI in src/api.cpp directly, so no kernel code is involved.
 *
 * libnk64.h is deliberately not included here: its C headers are not C++-clean
 * (they use _Static_assert and put one declaration outside extern "C"). The
 * three ABI wrappers we need are declared locally with C linkage instead.
 */
#include "nullk/securekernel.h"

extern "C" {
uint32_t nk64_log(uint32_t level, const char *text, uint32_t len);
uint32_t nk64_sleep(uint32_t ticks);
uint32_t nk64_yield(void);
}

extern "C" void securekernel_service_entry(void)
{
    static const char online[] =
        "[securekernel] C++ userspace integrity layer online\n";

    (void)nk64_log(6u, online, (uint32_t)(sizeof(online) - 1u));
    for (;;) {
        (void)nk64_sleep(200);
        (void)nk64_yield();
    }
}
