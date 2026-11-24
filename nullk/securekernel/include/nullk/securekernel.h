/*
 * SecureKernel - userspace ELF integrity layer for NULLK ABI v3.
 *
 * SecureKernel never runs in ring 0. The tiny AMD64 core still owns only CPU,
 * memory, interrupts and scheduling; this layer is a C++ userspace service and
 * a small C ABI that exec callers use before asking the kernel to load a binary.
 *
 * Two gates run before exec:
 *   - Type enforcement: a labelled subject must be allowed to operate on the
 *     object context of the path (longest-prefix label, default DENY).
 *   - Integrity: a valid NKSIGv1 signature proves integrity; suspicious
 *     symbols/strings are always denied, even when signed; a broken signature
 *     is denied; an unsigned clean image is allowed with a warning unless
 *     NK_SECUREKERNEL_REQUIRE_SIGNED is set.
 */
#ifndef NULLK_SECUREKERNEL_H
#define NULLK_SECUREKERNEL_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Development key. Replace in production builds; keep signer and kernel in sync. */
#define NK_SECUREKERNEL_DEFAULT_KEY "nullkernel-dev-signing-key-0001x"
#define NK_SECUREKERNEL_DEFAULT_KEY_LEN 32u

#define NK_SECUREKERNEL_SIG_MAGIC "NKSIGv1"
#define NK_SECUREKERNEL_SIG_MAGIC_LEN 8u
#define NK_SECUREKERNEL_SIG_VERSION 1u

#define NK_SECUREKERNEL_MAX_IMAGE 0x20000u
#define NK_SECUREKERNEL_MAX_DETAIL 128u
#define NK_SECUREKERNEL_MAX_SUSPICIOUS 32u

/* Set to 1 in production builds to reject every unsigned image. */
#ifndef NK_SECUREKERNEL_REQUIRE_SIGNED
#define NK_SECUREKERNEL_REQUIRE_SIGNED 0
#endif

enum {
    NK_SECURE_DECISION_ALLOW = 0,
    NK_SECURE_DECISION_DENY = 1
};

enum {
    NK_SECURE_FLAG_SIGNED = 0x00000001u,
    NK_SECURE_FLAG_UNSIGNED = 0x00000002u,
    NK_SECURE_FLAG_BAD_SIGNATURE = 0x00000004u,
    NK_SECURE_FLAG_SUSPICIOUS = 0x00000008u,
    NK_SECURE_FLAG_BAD_ELF = 0x00000010u,
    NK_SECURE_FLAG_POLICY_DENY = 0x00000020u
};

typedef struct {
    uint32_t decision;
    uint32_t flags;
    uint32_t signature_status;
    uint32_t suspicious_count;
    uint32_t function_count;
    char detail[NK_SECUREKERNEL_MAX_DETAIL];
} nk_secure_report_t;

/* Return 0 when a report was produced. The decision is in report->decision. */
int nk_securekernel_verify(const uint8_t *image, uint32_t size,
                           nk_secure_report_t *report);

/*
 * Inject an NKSIGv1 section into an ELF64 image and return its new size.
 * Return 0 on success, 1 on bad arguments, 2 if already signed, 3 if the
 * output buffer is too small, 4 if the image is not a supported ELF64 file.
 */
int nk_securekernel_sign(uint8_t *image, uint32_t size, uint32_t cap,
                         uint32_t *out_size);

/*
 * Type-enforcement policy.
 *
 * Subjects are the labelled ring 3 tasks; objects are files labelled by the
 * longest matching path prefix. A rule grants (or explicitly denies) a set of
 * operations between a subject and an object context. If no rule applies the
 * default decision is DENY, so an unlabelled subject or path fails closed.
 */
enum {
    NK_SECURE_OP_EXEC = 0u,
    NK_SECURE_OP_READ = 1u,
    NK_SECURE_OP_WRITE = 2u,
    NK_SECURE_OP_LOAD = 3u,     /* USB / UART image loading */
    NK_SECURE_OP_COUNT = 4u
};

#define NK_SECURE_OP_BIT(op) (1u << (op))

#define NK_SECURE_LABEL_MAX 24u
#define NK_SECURE_PATH_MAX 64u
#define NK_SECURE_MAX_RULES 32u
#define NK_SECURE_MAX_PATH_RULES 16u
#define NK_SECURE_MAX_AUDIT 16u

#define NK_SECURE_SUBJECT_INIT      "init_t"
#define NK_SECURE_SUBJECT_SHELL     "shell_t"
#define NK_SECURE_SUBJECT_SERVICE   "service_t"
#define NK_SECURE_SUBJECT_UNTRUSTED "untrusted_t"
#define NK_SECURE_OBJECT_SYSTEM     "system_t"
#define NK_SECURE_OBJECT_USER       "user_t"
#define NK_SECURE_OBJECT_USB        "usb_t"
#define NK_SECURE_OBJECT_CONFIG     "config_t"
#define NK_SECURE_OBJECT_UNKNOWN    "unknown_t"

typedef struct {
    char name[NK_SECURE_LABEL_MAX];
} nk_secure_label_t;

typedef struct {
    nk_secure_label_t subject;
    nk_secure_label_t object;
    uint32_t operations;        /* bitmask of NK_SECURE_OP_BIT() */
    uint8_t decision;           /* NK_SECURE_DECISION_ALLOW / DENY */
    uint8_t reserved[3];
} nk_secure_rule_t;

typedef struct {
    char prefix[NK_SECURE_PATH_MAX];  /* e.g. "@/data/system/" */
    nk_secure_label_t object;
} nk_secure_path_rule_t;

typedef struct {
    nk_secure_rule_t rules[NK_SECURE_MAX_RULES];
    uint32_t rule_count;
    nk_secure_path_rule_t paths[NK_SECURE_MAX_PATH_RULES];
    uint32_t path_count;
    uint8_t default_decision;
    uint8_t reserved[3];
} nk_secure_policy_t;

typedef struct {
    uint32_t sequence;
    char subject[NK_SECURE_LABEL_MAX];
    char object[NK_SECURE_LABEL_MAX];
    uint32_t operation;
    uint32_t decision;
    char reason[NK_SECUREKERNEL_MAX_DETAIL];
} nk_secure_audit_t;

/* Copy the built-in default policy. Returns 0 on success. */
int nk_securekernel_policy_builtin(nk_secure_policy_t *out);

/*
 * Install a policy. Returns 0 on success, 1 for a malformed policy, or 2 if
 * the policy has been locked by the service. Call this before starting tasks.
 */
int nk_securekernel_policy_set(const nk_secure_policy_t *policy);
int nk_securekernel_policy_lock(void);
int nk_securekernel_policy_is_locked(void);

/* Label a path with the active policy's longest-prefix rule. */
int nk_securekernel_label_path(const char *path, nk_secure_label_t *out);

/* Type enforcement only. The report carries the decision and a reason. */
int nk_securekernel_check(const char *subject, const char *path,
                          uint32_t operation, nk_secure_report_t *report);

/* Full gate: type enforcement plus ELF integrity scan/signature check. */
int nk_securekernel_enforce(const char *subject, const char *path,
                            const uint8_t *image, uint32_t size,
                            nk_secure_report_t *report);

/* Bounded audit ring. audit_format() renders an entry for the kernel log. */
uint32_t nk_securekernel_audit_count(void);
int nk_securekernel_audit_last(nk_secure_audit_t *out);
uint32_t nk_securekernel_audit_drain(nk_secure_audit_t *out, uint32_t max);
uint32_t nk_securekernel_audit_format(const nk_secure_audit_t *entry,
                                      char *out, uint32_t cap);

/* Ring 3 service entry point: loads the policy and drains the audit ring. */
void securekernel_service_entry(void);

#ifdef __cplusplus
}
#endif

#endif /* NULLK_SECUREKERNEL_H */
