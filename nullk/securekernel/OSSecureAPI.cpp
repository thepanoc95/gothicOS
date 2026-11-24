/*
 * SecureKernel userspace service entry.
 *
 * The layer itself is a C++ library linked into ring 3 tasks. This service
 * installs and locks the default type-enforcement policy at boot, then drains
 * the shared audit ring into the kernel log. Exec callers use the C ABI in
 * src/api.cpp directly, so no ring-0 code is involved.
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
        "securekernel: integrity layer online\n";
    static const char policy_ok[] =
        "securekernel: policy: v2 loaded (type enforcement, default-deny)\n";
    static const char policy_fail[] =
        "securekernel: policy: load failed; exec is fail-closed\n";

    nk_secure_policy_t policy;

    if (nk_securekernel_policy_builtin(&policy) == 0 &&
        nk_securekernel_policy_set(&policy) == 0 &&
        nk_securekernel_policy_lock() == 0) {
        (void)nk64_log(6u, policy_ok, (uint32_t)(sizeof(policy_ok) - 1u));
    } else {
        (void)nk64_log(4u, policy_fail, (uint32_t)(sizeof(policy_fail) - 1u));
    }
    (void)nk64_log(6u, online, (uint32_t)(sizeof(online) - 1u));

    for (;;) {
        nk_secure_audit_t batch[4];
        uint32_t count = nk_securekernel_audit_drain(batch, 4u);

        for (uint32_t i = 0u; i < count; i++) {
            char line[NK_SECUREKERNEL_MAX_DETAIL + 64u];
            uint32_t len = nk_securekernel_audit_format(&batch[i], line,
                                                        (uint32_t)sizeof(line));
            uint32_t level = batch[i].decision ==
                             (uint32_t)NK_SECURE_DECISION_DENY ? 4u : 6u;

            if (len != 0u) {
                (void)nk64_log(level, line, len);
            }
        }
        (void)nk64_sleep(50u);
        (void)nk64_yield();
    }
}
