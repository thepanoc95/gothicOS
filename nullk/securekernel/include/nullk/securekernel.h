/*
 * SecureKernel - userspace ELF integrity layer for NULLK ABI v3.
 *
 * SecureKernel never runs in ring 0. The tiny AMD64 core still owns only CPU,
 * memory, interrupts and scheduling; this layer is a C++ userspace service and
 * a small C ABI that exec callers use before asking the kernel to load a binary.
 *
 * Policy:
 *   - A valid NKSIGv1 signature proves integrity and yields ALLOW.
 *   - Suspicious symbols/strings are always denied, even when signed.
 *   - A broken signature is denied (tampering).
 *   - An unsigned image is scanned; if clean it is allowed with a warning.
 *     Build with NK_SECUREKERNEL_REQUIRE_SIGNED to reject all unsigned images.
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
    NK_SECURE_FLAG_BAD_ELF = 0x00000010u
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

/* Ring 3 service entry point; logs that the integrity layer is online. */
void securekernel_service_entry(void);

#ifdef __cplusplus
}
#endif

#endif /* NULLK_SECUREKERNEL_H */
