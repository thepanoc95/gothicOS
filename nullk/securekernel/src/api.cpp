/* C ABI wrappers with no platform dependencies (host tests link this too). */
#include "nullk/securekernel.h"
#include "engine.h"

extern "C" int nk_securekernel_verify(const uint8_t *image, uint32_t size,
                                      nk_secure_report_t *report)
{
    return nksecure::verify_image(image, size, report);
}

extern "C" int nk_securekernel_sign(uint8_t *image, uint32_t size, uint32_t cap,
                                    uint32_t *out_size)
{
    return nksecure::sign_image(image, size, cap, out_size);
}

extern "C" int nk_securekernel_policy_builtin(nk_secure_policy_t *out)
{
    return nksecure::policy_builtin(out);
}

extern "C" int nk_securekernel_policy_set(const nk_secure_policy_t *policy)
{
    return nksecure::policy_set(policy);
}

extern "C" int nk_securekernel_policy_lock(void)
{
    return nksecure::policy_lock();
}

extern "C" int nk_securekernel_policy_is_locked(void)
{
    return nksecure::policy_is_locked();
}

extern "C" int nk_securekernel_label_path(const char *path,
                                          nk_secure_label_t *out)
{
    return nksecure::label_path(path, out);
}

extern "C" int nk_securekernel_check(const char *subject, const char *path,
                                     uint32_t operation,
                                     nk_secure_report_t *report)
{
    return nksecure::access_report(subject, path, operation, report);
}

extern "C" int nk_securekernel_enforce(const char *subject, const char *path,
                                       const uint8_t *image, uint32_t size,
                                       nk_secure_report_t *report)
{
    return nksecure::enforce_image(subject, path, image, size, report);
}

extern "C" uint32_t nk_securekernel_audit_count(void)
{
    return nksecure::audit_count();
}

extern "C" int nk_securekernel_audit_last(nk_secure_audit_t *out)
{
    return nksecure::audit_last(out);
}

extern "C" uint32_t nk_securekernel_audit_drain(nk_secure_audit_t *out,
                                                uint32_t max)
{
    return nksecure::audit_drain(out, max);
}

extern "C" uint32_t nk_securekernel_audit_format(const nk_secure_audit_t *entry,
                                                 char *out, uint32_t cap)
{
    return nksecure::audit_format(entry, out, cap);
}
