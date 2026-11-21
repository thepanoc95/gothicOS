/* Internal SecureKernel engine declarations (not part of the C ABI). */
#ifndef NULLK_SECURE_ENGINE_H
#define NULLK_SECURE_ENGINE_H

#include <stdint.h>
#include "nullk/securekernel.h"

namespace nksecure {

/* Integrity engine (engine.cpp). */
int verify_image(const uint8_t *image, uint32_t size, nk_secure_report_t *report);
int sign_image(uint8_t *image, uint32_t size, uint32_t cap, uint32_t *out_size);

/* Bounded report helpers, also used by the policy engine. */
void report_detail(nk_secure_report_t *report, const char *text);

/* Type enforcement and audit engine (policy.cpp). */
int policy_builtin(nk_secure_policy_t *out);
int policy_set(const nk_secure_policy_t *policy);
int policy_lock(void);
int policy_is_locked(void);
int label_path(const char *path, nk_secure_label_t *out);
int decide_access(const char *subject, const char *path, uint32_t operation,
                  nk_secure_report_t *report);
int access_report(const char *subject, const char *path, uint32_t operation,
                  nk_secure_report_t *report);
int enforce_image(const char *subject, const char *path,
                  const uint8_t *image, uint32_t size,
                  nk_secure_report_t *report);
void audit_record(const char *subject, const char *object, uint32_t operation,
                  uint32_t decision, const char *reason);
uint32_t audit_count(void);
int audit_last(nk_secure_audit_t *out);
uint32_t audit_drain(nk_secure_audit_t *out, uint32_t max);
uint32_t audit_format(const nk_secure_audit_t *entry, char *out, uint32_t cap);

} /* namespace nksecure */

#endif /* NULLK_SECURE_ENGINE_H */
