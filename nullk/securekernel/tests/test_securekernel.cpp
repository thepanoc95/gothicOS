/*
 * Hosted tests for the C++ SecureKernel layer. These build tiny ELF64 images
 * with symbol tables, exercise the public C ABI, and check SHA-256 against a
 * standard test vector.
 */
#include <cstdio>
#include <cstring>

#include "nullk/securekernel.h"
#include "src/sha256.h"

static int g_checks;
static int g_passed;

#define CHECK(name, expr)                                                      \
    do {                                                                       \
        g_checks++;                                                            \
        if (expr) {                                                            \
            g_passed++;                                                        \
            std::printf("PASS %s\n", name);                                    \
        } else {                                                               \
            std::printf("FAIL %s (line %d)\n", name, __LINE__);                \
        }                                                                      \
    } while (0)

static void put16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
}

static void put32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

static void put64(uint8_t *p, uint64_t v)
{
    put32(p, (uint32_t)v);
    put32(p + 4, (uint32_t)(v >> 32));
}

static void write_section(uint8_t *sh, uint32_t type, uint32_t offset,
                          uint32_t size, uint32_t link)
{
    std::memset(sh, 0, 64);
    put32(sh + 4, type);
    put64(sh + 24, offset);
    put64(sh + 32, size);
    put32(sh + 40, link);
    put64(sh + 48, 1u);
}

/* Build an ET_EXEC image with one safe function and, optionally, a HACK one. */
static uint32_t make_elf(uint8_t *buf, bool suspicious)
{
    static const char shstr[] = "\0.shstrtab\0.strtab\0.symtab\0";
    static const char strs_clean[] = "\0safe_func\0";
    static const char strs_bad[] = "\0safe_func\0HACK_gate\0";
    const char *strs = suspicious ? strs_bad : strs_clean;
    const uint32_t shstr_off = 320u;
    const uint32_t str_off = 352u;
    const uint32_t sym_off = 384u;
    const uint32_t shstr_len = (uint32_t)sizeof(shstr);
    const uint32_t str_len = suspicious ? (uint32_t)sizeof(strs_bad)
                                        : (uint32_t)sizeof(strs_clean);
    const uint32_t sym_len = 72u;

    std::memset(buf, 0, 512);

    buf[0] = 0x7f; buf[1] = 'E'; buf[2] = 'L'; buf[3] = 'F';
    buf[4] = 2; buf[5] = 1; buf[6] = 1;
    put16(buf + 16, 2u);
    put16(buf + 18, 62u);
    put32(buf + 20, 1u);
    put64(buf + 40, 64u);
    put16(buf + 52, 64u);
    put16(buf + 54, 56u);
    put16(buf + 56, 0u);
    put16(buf + 58, 64u);
    put16(buf + 60, 4u);
    put16(buf + 62, 1u);

    write_section(buf + 64 + 1u * 64u, 3u, shstr_off, shstr_len, 0u);
    write_section(buf + 64 + 2u * 64u, 3u, str_off, str_len, 0u);
    write_section(buf + 64 + 3u * 64u, 2u, sym_off, sym_len, 2u);

    std::memcpy(buf + shstr_off, shstr, shstr_len);
    std::memcpy(buf + str_off, strs, str_len);

    /* Symbol 1: safe_func, GLOBAL FUNC. */
    put32(buf + sym_off + 24u, 1u);
    buf[sym_off + 24u + 4u] = 0x12u;
    put64(buf + sym_off + 24u + 8u, 0x400000u);

    if (suspicious) {
        put32(buf + sym_off + 48u, 11u);
        buf[sym_off + 48u + 4u] = 0x12u;
        put64(buf + sym_off + 48u + 8u, 0x400010u);
    }

    return sym_off + sym_len;
}

static void test_sha256(void)
{
    uint8_t digest[32];
    static const uint8_t expected[32] = {
        0xba, 0x78, 0x16, 0xbf, 0x8f, 0x01, 0xcf, 0xea,
        0x41, 0x41, 0x40, 0xde, 0x5d, 0xae, 0x22, 0x23,
        0xb0, 0x03, 0x61, 0xa3, 0x96, 0x17, 0x7a, 0x9c,
        0xb4, 0x10, 0xff, 0x61, 0xf2, 0x00, 0x15, 0xad
    };
    nksecure::Sha256 sha;

    sha.init();
    sha.update((const uint8_t *)"abc", 3u);
    sha.finish(digest);
    CHECK("SHA-256 vector", std::memcmp(digest, expected, 32u) == 0);
}

static void test_unsigned_and_scan(void)
{
    uint8_t image[512];
    nk_secure_report_t report;
    uint32_t size = make_elf(image, false);

    CHECK("verify clean image",
          nk_securekernel_verify(image, size, &report) == 0);
    CHECK("clean image allowed",
          report.decision == (uint32_t)NK_SECURE_DECISION_ALLOW);
    CHECK("clean image flagged unsigned",
          (report.flags & (uint32_t)NK_SECURE_FLAG_UNSIGNED) != 0u);
    CHECK("function symbols counted", report.function_count == 1u);
    CHECK("suspicious count is zero", report.suspicious_count == 0u);

    size = make_elf(image, true);
    CHECK("verify suspicious image",
          nk_securekernel_verify(image, size, &report) == 0);
    CHECK("suspicious image denied",
          report.decision == (uint32_t)NK_SECURE_DECISION_DENY);
    CHECK("suspicious flag set",
          (report.flags & (uint32_t)NK_SECURE_FLAG_SUSPICIOUS) != 0u);
    CHECK("suspicious token reported",
          std::strstr(report.detail, "HACK") != nullptr);
}

static void test_signing(void)
{
    uint8_t clean[4096];
    uint8_t signed_image[4096];
    nk_secure_report_t report;
    uint32_t clean_size = make_elf(clean, false);
    uint32_t signed_size = 0u;

    std::memcpy(signed_image, clean, clean_size);
    CHECK("sign clean ELF",
          nk_securekernel_sign(signed_image, clean_size,
                               (uint32_t)sizeof(signed_image),
                               &signed_size) == 0);
    CHECK("signature increases size", signed_size > clean_size);

    CHECK("verify signed ELF",
          nk_securekernel_verify(signed_image, signed_size, &report) == 0);
    CHECK("signed image allowed",
          report.decision == (uint32_t)NK_SECURE_DECISION_ALLOW);
    CHECK("signed flag set",
          (report.flags & (uint32_t)NK_SECURE_FLAG_SIGNED) != 0u);
    CHECK("unsigned flag clear",
          (report.flags & (uint32_t)NK_SECURE_FLAG_UNSIGNED) == 0u);

    signed_image[52] ^= 0x01u;
    CHECK("verify tampered ELF",
          nk_securekernel_verify(signed_image, signed_size, &report) == 0);
    CHECK("tampered image denied",
          report.decision == (uint32_t)NK_SECURE_DECISION_DENY);
    CHECK("bad signature flag set",
          (report.flags & (uint32_t)NK_SECURE_FLAG_BAD_SIGNATURE) != 0u);
    CHECK("double signing rejected",
          nk_securekernel_sign(signed_image, signed_size,
                               (uint32_t)sizeof(signed_image),
                               &signed_size) == 2);
}

static void test_bad_input(void)
{
    uint8_t junk[32];
    nk_secure_report_t report;

    std::memset(junk, 0, sizeof(junk));
    CHECK("verify junk image",
          nk_securekernel_verify(junk, (uint32_t)sizeof(junk), &report) == 0);
    CHECK("junk denied",
          report.decision == (uint32_t)NK_SECURE_DECISION_DENY);
    CHECK("bad ELF flag set",
          (report.flags & (uint32_t)NK_SECURE_FLAG_BAD_ELF) != 0u);
    CHECK("null report rejected",
          nk_securekernel_verify(junk, (uint32_t)sizeof(junk), nullptr) != 0);
}

static void set_label(char *dst, const char *name)
{
    std::strncpy(dst, name, NK_SECURE_LABEL_MAX - 1u);
    dst[NK_SECURE_LABEL_MAX - 1u] = '\0';
}

static void test_policy_builtin(void)
{
    nk_secure_policy_t policy;
    nk_secure_label_t label;
    nk_secure_report_t report;

    CHECK("builtin policy available",
          nk_securekernel_policy_builtin(&policy) == 0 &&
              policy.rule_count > 0u && policy.path_count > 0u);
    CHECK("builtin policy is default-deny",
          policy.default_decision == (uint8_t)NK_SECURE_DECISION_DENY);

    CHECK("system path labelled",
          nk_securekernel_label_path("@/data/system/null_kernel.elf",
                                     &label) == 0 &&
              std::strcmp(label.name, NK_SECURE_OBJECT_SYSTEM) == 0);
    CHECK("user path labelled",
          nk_securekernel_label_path("@/data/user/app.elf", &label) == 0 &&
              std::strcmp(label.name, NK_SECURE_OBJECT_USER) == 0);
    CHECK("usb path labelled",
          nk_securekernel_label_path("@/usb/program.elf", &label) == 0 &&
              std::strcmp(label.name, NK_SECURE_OBJECT_USB) == 0);
    CHECK("unknown path labelled",
          nk_securekernel_label_path("@/elsewhere/x", &label) == 0 &&
              std::strcmp(label.name, NK_SECURE_OBJECT_UNKNOWN) == 0);

    CHECK("shell cannot exec system image",
          nk_securekernel_check(NK_SECURE_SUBJECT_SHELL,
                                "@/data/system/null_kernel.elf",
                                (uint32_t)NK_SECURE_OP_EXEC, &report) == 0 &&
              report.decision == (uint32_t)NK_SECURE_DECISION_DENY &&
              (report.flags & (uint32_t)NK_SECURE_FLAG_POLICY_DENY) != 0u);
    CHECK("shell can exec user image",
          nk_securekernel_check(NK_SECURE_SUBJECT_SHELL,
                                "@/data/user/app.elf",
                                (uint32_t)NK_SECURE_OP_EXEC, &report) == 0 &&
              report.decision == (uint32_t)NK_SECURE_DECISION_ALLOW);
    CHECK("shell can read system image",
          nk_securekernel_check(NK_SECURE_SUBJECT_SHELL,
                                "@/data/system/null_kernel.elf",
                                (uint32_t)NK_SECURE_OP_READ, &report) == 0 &&
              report.decision == (uint32_t)NK_SECURE_DECISION_ALLOW);
    CHECK("shell cannot write user image",
          nk_securekernel_check(NK_SECURE_SUBJECT_SHELL,
                                "@/data/user/app.elf",
                                (uint32_t)NK_SECURE_OP_WRITE, &report) == 0 &&
              report.decision == (uint32_t)NK_SECURE_DECISION_DENY);
    CHECK("service can load usb payload",
          nk_securekernel_check(NK_SECURE_SUBJECT_SERVICE,
                                "@/usb/program.elf",
                                (uint32_t)NK_SECURE_OP_LOAD, &report) == 0 &&
              report.decision == (uint32_t)NK_SECURE_DECISION_ALLOW);
    CHECK("unknown subject denied",
          nk_securekernel_check("nobody_t", "@/data/user/app.elf",
                                (uint32_t)NK_SECURE_OP_EXEC, &report) == 0 &&
              report.decision == (uint32_t)NK_SECURE_DECISION_DENY);
}

static void test_policy_custom(void)
{
    nk_secure_policy_t policy;
    nk_secure_report_t report;

    std::memset(&policy, 0, sizeof(policy));
    policy.default_decision = (uint8_t)NK_SECURE_DECISION_DENY;
    policy.path_count = 1u;
    std::strncpy(policy.paths[0].prefix, "@/allow/",
                 NK_SECURE_PATH_MAX - 1u);
    policy.paths[0].prefix[NK_SECURE_PATH_MAX - 1u] = '\0';
    set_label(policy.paths[0].object.name, NK_SECURE_OBJECT_USER);

    policy.rule_count = 1u;
    set_label(policy.rules[0].subject.name, NK_SECURE_SUBJECT_SHELL);
    set_label(policy.rules[0].object.name, NK_SECURE_OBJECT_USER);
    policy.rules[0].operations = (uint32_t)NK_SECURE_OP_BIT(NK_SECURE_OP_EXEC);
    policy.rules[0].decision = (uint8_t)NK_SECURE_DECISION_ALLOW;

    CHECK("install custom policy",
          nk_securekernel_policy_set(&policy) == 0);
    CHECK("custom allow applies",
          nk_securekernel_check(NK_SECURE_SUBJECT_SHELL, "@/allow/app.elf",
                                (uint32_t)NK_SECURE_OP_EXEC, &report) == 0 &&
              report.decision == (uint32_t)NK_SECURE_DECISION_ALLOW);
    CHECK("custom default denies other paths",
          nk_securekernel_check(NK_SECURE_SUBJECT_SHELL,
                                "@/data/user/app.elf",
                                (uint32_t)NK_SECURE_OP_EXEC, &report) == 0 &&
              report.decision == (uint32_t)NK_SECURE_DECISION_DENY);

    policy.rules[0].operations = 1u << 12u;
    CHECK("malformed policy rejected",
          nk_securekernel_policy_set(&policy) == 1);

    CHECK("builtin policy restored",
          nk_securekernel_policy_builtin(&policy) == 0 &&
              nk_securekernel_policy_set(&policy) == 0);
}

static void test_enforce(void)
{
    uint8_t clean[512];
    uint8_t suspicious[512];
    nk_secure_report_t report;
    uint32_t size = make_elf(clean, false);

    CHECK("enforce clean user exec",
          nk_securekernel_enforce(NK_SECURE_SUBJECT_SHELL,
                                  "@/data/user/app.elf", clean, size,
                                  &report) == 0 &&
              report.decision == (uint32_t)NK_SECURE_DECISION_ALLOW);

    std::memcpy(suspicious, clean, size);
    size = make_elf(suspicious, true);
    CHECK("enforce suspicious user exec",
          nk_securekernel_enforce(NK_SECURE_SUBJECT_SHELL,
                                  "@/data/user/app.elf", suspicious, size,
                                  &report) == 0 &&
              report.decision == (uint32_t)NK_SECURE_DECISION_DENY &&
              (report.flags & (uint32_t)NK_SECURE_FLAG_SUSPICIOUS) != 0u);

    size = make_elf(clean, false);
    CHECK("enforce policy-denied system exec",
          nk_securekernel_enforce(NK_SECURE_SUBJECT_SHELL,
                                  "@/data/system/null_kernel.elf", clean,
                                  size, &report) == 0 &&
              report.decision == (uint32_t)NK_SECURE_DECISION_DENY &&
              (report.flags & (uint32_t)NK_SECURE_FLAG_POLICY_DENY) != 0u);
}

static void test_audit(void)
{
    nk_secure_audit_t entries[NK_SECURE_MAX_AUDIT + 2u];
    nk_secure_audit_t last;
    nk_secure_report_t report;
    char line[NK_SECUREKERNEL_MAX_DETAIL + 64u];
    uint32_t before = nk_securekernel_audit_count();
    uint32_t drained;
    int found_deny = 0;

    (void)nk_securekernel_check(NK_SECURE_SUBJECT_SHELL,
                                "@/data/system/null_kernel.elf",
                                (uint32_t)NK_SECURE_OP_EXEC, &report);
    CHECK("audit count grows",
          nk_securekernel_audit_count() > before);
    CHECK("audit last entry",
          nk_securekernel_audit_last(&last) == 0 &&
              std::strcmp(last.subject, NK_SECURE_SUBJECT_SHELL) == 0 &&
              std::strcmp(last.object, NK_SECURE_OBJECT_SYSTEM) == 0);

    drained = nk_securekernel_audit_drain(entries, NK_SECURE_MAX_AUDIT + 2u);
    CHECK("audit drain bounded",
          drained > 0u && drained <= (uint32_t)NK_SECURE_MAX_AUDIT);
    for (uint32_t i = 0u; i < drained; i++) {
        if (entries[i].decision == (uint32_t)NK_SECURE_DECISION_DENY) {
            found_deny = 1;
            CHECK("audit line formatted",
                  nk_securekernel_audit_format(&entries[i], line,
                                               (uint32_t)sizeof(line)) > 0u &&
                      std::strstr(line, "DENY") != nullptr &&
                      std::strstr(line, entries[i].subject) != nullptr);
        }
    }
    CHECK("audit recorded denial", found_deny != 0);
    CHECK("audit drained empty", nk_securekernel_audit_count() == 0u);
    CHECK("audit last on empty", nk_securekernel_audit_last(&last) != 0);
}

static void test_policy_lock(void)
{
    nk_secure_policy_t policy;

    CHECK("lock policy", nk_securekernel_policy_lock() == 0);
    CHECK("policy is locked", nk_securekernel_policy_is_locked() != 0);
    CHECK("locked policy rejects changes",
          nk_securekernel_policy_builtin(&policy) == 0 &&
              nk_securekernel_policy_set(&policy) == 2);
}

int main(void)
{
    test_sha256();
    test_unsigned_and_scan();
    test_signing();
    test_bad_input();
    test_policy_builtin();
    test_policy_custom();
    test_enforce();
    test_audit();
    test_policy_lock();

    std::printf("securekernel host tests: %d/%d passed\n", g_passed, g_checks);
    return (g_passed == g_checks) ? 0 : 1;
}
