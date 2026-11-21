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

int main(void)
{
    test_sha256();
    test_unsigned_and_scan();
    test_signing();
    test_bad_input();

    std::printf("securekernel host tests: %d/%d passed\n", g_passed, g_checks);
    return (g_passed == g_checks) ? 0 : 1;
}
