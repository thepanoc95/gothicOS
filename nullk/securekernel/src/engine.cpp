#include "engine.h"
#include "sha256.h"

namespace nksecure {

namespace {

const uint32_t kSignatureSize = 88u;
const uint32_t kSigVersionOff = 8u;
const uint32_t kSigFlagsOff = 12u;
const uint32_t kSigImageSizeOff = 16u;
const uint32_t kSigHashOff = 24u;
const uint32_t kSigMacOff = 56u;

const uint16_t kShtProgbits = 1u;
const uint16_t kShtSymtab = 2u;
const uint16_t kShtStrtab = 3u;
const uint16_t kShtDynsym = 11u;
const uint64_t kShfExecinstr = 0x4u;
const uint8_t kSttFunc = 2u;

const char *const kSuspicious[] = {
    "HACK", "BREAK", "SYSCALL", "INJECT", "EXPLOIT", "ROOTKIT",
    "PAYLOAD", "BYPASS", "SPOOF", "PATCH", "TRAMPOLINE"
};

void mem_zero(uint8_t *dst, uint32_t len)
{
    for (uint32_t i = 0u; i < len; i++) {
        dst[i] = 0u;
    }
}

void mem_copy(uint8_t *dst, const uint8_t *src, uint32_t len)
{
    for (uint32_t i = 0u; i < len; i++) {
        dst[i] = src[i];
    }
}

int mem_equal(const uint8_t *a, const uint8_t *b, uint32_t len)
{
    uint8_t diff = 0u;

    for (uint32_t i = 0u; i < len; i++) {
        diff = (uint8_t)(diff | (a[i] ^ b[i]));
    }
    return diff == 0u ? 1 : 0;
}

uint16_t rd16(const uint8_t *p)
{
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

uint32_t rd32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

uint64_t rd64(const uint8_t *p)
{
    return (uint64_t)rd32(p) | ((uint64_t)rd32(p + 4) << 32);
}

void wr16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
}

void wr32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

void wr64(uint8_t *p, uint64_t v)
{
    wr32(p, (uint32_t)v);
    wr32(p + 4, (uint32_t)(v >> 32));
}

uint32_t align8(uint32_t v)
{
    return (v + 7u) & ~7u;
}

int valid_elf(const uint8_t *image, uint32_t size, uint32_t *shoff,
              uint16_t *shentsize, uint16_t *shnum)
{
    uint64_t raw_shoff;
    uint64_t table_end;

    if (image == 0 || size < 64u) {
        return 0;
    }
    if (image[0] != 0x7fu || image[1] != 'E' || image[2] != 'L' ||
        image[3] != 'F' || image[4] != 2u || image[5] != 1u) {
        return 0;
    }
    if (rd16(image + 16) != 2u || rd16(image + 18) != 62u) {
        return 0;
    }

    raw_shoff = rd64(image + 40);
    *shentsize = rd16(image + 58);
    *shnum = rd16(image + 60);
    if (*shnum == 0u || *shentsize < 64u || raw_shoff == 0u ||
        raw_shoff > (uint64_t)size) {
        return 0;
    }
    table_end = raw_shoff + (uint64_t)*shnum * (uint64_t)*shentsize;
    if (table_end < raw_shoff || table_end > (uint64_t)size) {
        return 0;
    }
    *shoff = (uint32_t)raw_shoff;
    return 1;
}

struct Section {
    uint32_t name;
    uint32_t type;
    uint64_t flags;
    uint64_t offset;
    uint64_t size;
    uint32_t link;
    uint64_t entsize;
};

int read_section(const uint8_t *image, uint32_t size, uint32_t shoff,
                 uint16_t shentsize, uint16_t index, Section *out)
{
    const uint8_t *p;
    uint64_t start;
    uint64_t end;

    if ((uint64_t)shoff > (uint64_t)size) {
        return 0;
    }
    start = (uint64_t)shoff + (uint64_t)index * (uint64_t)shentsize;
    if (start > (uint64_t)size ||
        (uint64_t)shentsize > (uint64_t)size - start) {
        return 0;
    }
    p = image + (uint32_t)start;
    out->name = rd32(p);
    out->type = rd32(p + 4);
    out->flags = rd64(p + 8);
    out->offset = rd64(p + 24);
    out->size = rd64(p + 32);
    out->link = rd32(p + 40);
    out->entsize = rd64(p + 56);
    end = out->offset + out->size;
    if (end < out->offset || end > (uint64_t)size) {
        return 0;
    }
    return 1;
}

struct SignatureLocation {
    uint32_t section_offset;
    uint32_t section_index;
};

int find_signature(const uint8_t *image, uint32_t size, uint32_t shoff,
                   uint16_t shentsize, uint16_t shnum,
                   SignatureLocation *out)
{
    static const uint8_t magic[8] = {
        'N', 'K', 'S', 'I', 'G', 'v', '1', 0
    };

    for (uint32_t i = 0u; i < shnum; i++) {
        Section sec;

        if (!read_section(image, size, shoff, shentsize, (uint16_t)i, &sec)) {
            continue;
        }
        if (sec.size < kSignatureSize || sec.offset > (uint64_t)size ||
            sec.name != 0u || sec.type != kShtProgbits) {
            continue;
        }
        if (mem_equal(image + (uint32_t)sec.offset, magic, 8u)) {
            out->section_offset = (uint32_t)sec.offset;
            out->section_index = i;
            return 1;
        }
    }
    return 0;
}

int contains_token(const uint8_t *data, uint32_t len, const char *token)
{
    uint32_t n = 0u;

    while (token[n] != '\0') {
        n++;
    }
    if (n == 0u || len < n) {
        return 0;
    }
    for (uint32_t i = 0u; i + n <= len; i++) {
        uint32_t j = 0u;

        while (j < n && (char)data[i + j] == token[j]) {
            j++;
        }
        if (j == n) {
            return 1;
        }
    }
    return 0;
}

const char *scan_bytes(const uint8_t *data, uint32_t len)
{
    for (uint32_t i = 0u; i < sizeof(kSuspicious) / sizeof(kSuspicious[0]); i++) {
        if (contains_token(data, len, kSuspicious[i])) {
            return kSuspicious[i];
        }
    }
    return 0;
}

void detail_copy(char *dst, const char *text)
{
    uint32_t i = 0u;

    if (dst == 0) {
        return;
    }
    dst[0] = '\0';
    if (text == 0) {
        return;
    }
    while (text[i] != '\0' && i + 1u < NK_SECUREKERNEL_MAX_DETAIL) {
        dst[i] = text[i];
        i++;
    }
    dst[i] = '\0';
}

void detail_append(char *dst, const char *text)
{
    uint32_t i = 0u;
    uint32_t j = 0u;

    if (dst == 0 || text == 0) {
        return;
    }
    while (dst[i] != '\0') {
        i++;
    }
    while (text[j] != '\0' && i + 1u < NK_SECUREKERNEL_MAX_DETAIL) {
        dst[i++] = text[j++];
    }
    dst[i] = '\0';
}

void record_hit(nk_secure_report_t *report, uint32_t *hits,
                const char *token)
{
    if (token == 0) {
        return;
    }
    if (*hits < NK_SECUREKERNEL_MAX_SUSPICIOUS) {
        (*hits)++;
    }
    if (report->detail[0] == '\0') {
        detail_copy(report->detail, "suspicious token: ");
        detail_append(report->detail, token);
    }
}

} /* namespace */

void report_detail(nk_secure_report_t *report, const char *text)
{
    detail_copy(report == 0 ? 0 : report->detail, text);
}

int verify_image(const uint8_t *image, uint32_t size, nk_secure_report_t *report)
{
    uint32_t shoff;
    uint16_t shentsize;
    uint16_t shnum;
    uint32_t hits = 0u;
    uint32_t functions = 0u;
    SignatureLocation sig;
    int have_sig;
    int signature_ok = 0;

    if (report == 0) {
        return 1;
    }
    mem_zero((uint8_t *)report, (uint32_t)sizeof(*report));
    report->decision = (uint32_t)NK_SECURE_DECISION_DENY;

    if (!valid_elf(image, size, &shoff, &shentsize, &shnum)) {
        report->flags |= (uint32_t)NK_SECURE_FLAG_BAD_ELF;
        detail_copy(report->detail, "unsupported ELF64 image");
        return 0;
    }

    for (uint32_t i = 0u; i < shnum; i++) {
        Section sec;
        const char *token;

        if (!read_section(image, size, shoff, shentsize, (uint16_t)i, &sec)) {
            continue;
        }

        if (sec.type == kShtSymtab || sec.type == kShtDynsym) {
            Section strings;

            if (sec.link < shnum &&
                read_section(image, size, shoff, shentsize,
                             (uint16_t)sec.link, &strings) &&
                strings.type == kShtStrtab) {
                uint32_t stride = 0u;
                uint32_t entry_size = 24u;

                if (sec.entsize >= 24u && sec.entsize <= 1024u) {
                    entry_size = (uint32_t)sec.entsize;
                }
                while (stride + 24u <= sec.size) {
                    uint32_t name_off = rd32(image + (uint32_t)sec.offset + stride);
                    uint8_t info = image[(uint32_t)sec.offset + stride + 4u];

                    if ((info & 0x0fu) == kSttFunc) {
                        functions++;
                    }
                    if (name_off < strings.size) {
                        uint32_t name_len = 0u;
                        const uint8_t *name = image + (uint32_t)strings.offset + name_off;
                        uint32_t avail = (uint32_t)(strings.size - name_off);

                        while (name_len < avail && name[name_len] != 0u) {
                            name_len++;
                        }
                        token = scan_bytes(name, name_len);
                        record_hit(report, &hits, token);
                    }
                    stride += entry_size;
                }
            }
        }

        if (sec.type == kShtStrtab || (sec.flags & kShfExecinstr) != 0u) {
            token = scan_bytes(image + (uint32_t)sec.offset,
                               (uint32_t)sec.size);
            record_hit(report, &hits, token);
        }
    }

    report->function_count = functions;
    report->suspicious_count = (hits < NK_SECUREKERNEL_MAX_SUSPICIOUS)
                                   ? hits : NK_SECUREKERNEL_MAX_SUSPICIOUS;
    if (hits != 0u) {
        report->flags |= (uint32_t)NK_SECURE_FLAG_SUSPICIOUS;
    }

    have_sig = find_signature(image, size, shoff, shentsize, shnum, &sig);
    if (have_sig) {
        uint8_t digest[32];
        uint8_t mac[32];
        uint64_t after = (uint64_t)sig.section_offset + kSignatureSize;
        uint64_t image_size = rd64(image + sig.section_offset +
                                   kSigImageSizeOff);
        const uint8_t *key = (const uint8_t *)NK_SECUREKERNEL_DEFAULT_KEY;

        report->signature_status = 2u;
        if (after <= (uint64_t)size &&
            rd32(image + sig.section_offset + kSigVersionOff) ==
                NK_SECUREKERNEL_SIG_VERSION &&
            image_size == (uint64_t)size - kSignatureSize) {
            uint32_t after_len = (uint32_t)((uint64_t)size - after);
            Sha256 sha;

            sha.init();
            sha.update(image, sig.section_offset);
            sha.update(image + after, after_len);
            sha.finish(digest);

            hmac_sha256(key, NK_SECUREKERNEL_DEFAULT_KEY_LEN,
                        image, sig.section_offset,
                        image + after, after_len, mac);

            if (mem_equal(digest,
                          image + sig.section_offset + kSigHashOff, 32u) &&
                mem_equal(mac,
                          image + sig.section_offset + kSigMacOff, 32u)) {
                signature_ok = 1;
                report->signature_status = 1u;
            }
        }
    }

    if (signature_ok) {
        report->flags |= (uint32_t)NK_SECURE_FLAG_SIGNED;
    } else if (have_sig) {
        report->flags |= (uint32_t)NK_SECURE_FLAG_BAD_SIGNATURE;
    } else {
        report->flags |= (uint32_t)NK_SECURE_FLAG_UNSIGNED;
    }

    if (hits != 0u || (have_sig && !signature_ok) ||
        (!have_sig && NK_SECUREKERNEL_REQUIRE_SIGNED != 0)) {
        report->decision = (uint32_t)NK_SECURE_DECISION_DENY;
        if (hits != 0u) {
            /* detail already names the token. */
        } else if (have_sig) {
            detail_copy(report->detail, "blocked: NKSIGv1 integrity check failed");
        } else {
            detail_copy(report->detail, "blocked: unsigned image");
        }
    } else {
        report->decision = (uint32_t)NK_SECURE_DECISION_ALLOW;
        if (signature_ok) {
            detail_copy(report->detail, "NKSIGv1 signature verified");
        } else {
            detail_copy(report->detail, "unsigned image passed policy scan");
        }
    }
    return 0;
}

int sign_image(uint8_t *image, uint32_t size, uint32_t cap, uint32_t *out_size)
{
    uint32_t shoff;
    uint16_t shentsize;
    uint16_t shnum;
    SignatureLocation existing;
    uint32_t old_bytes;
    uint32_t new_shoff;
    uint32_t new_shdr;
    uint32_t sig_off;
    uint32_t result_size;
    uint8_t digest[32];
    uint8_t mac[32];
    uint8_t *sig;
    uint8_t *shdr;
    const uint8_t *key = (const uint8_t *)NK_SECUREKERNEL_DEFAULT_KEY;

    if (image == 0 || out_size == 0) {
        return 1;
    }
    *out_size = 0u;
    if (!valid_elf(image, size, &shoff, &shentsize, &shnum)) {
        return 4;
    }
    if (find_signature(image, size, shoff, shentsize, shnum, &existing)) {
        return 2;
    }
    if (shnum == 0xffffu) {
        return 4;
    }

    old_bytes = (uint32_t)shnum * 64u;
    new_shoff = align8(size);
    new_shdr = new_shoff + old_bytes;
    sig_off = align8(new_shdr + 64u);
    result_size = sig_off + kSignatureSize;
    if (result_size > cap) {
        return 3;
    }

    mem_zero(image + size, result_size - size);
    mem_copy(image + new_shoff, image + shoff, old_bytes);
    mem_zero(image + shoff, old_bytes);

    wr64(image + 40, new_shoff);
    wr16(image + 60, (uint16_t)(shnum + 1u));

    shdr = image + new_shdr;
    mem_zero(shdr, 64u);
    wr32(shdr + 4, kShtProgbits);
    wr64(shdr + 24, sig_off);
    wr64(shdr + 32, kSignatureSize);
    wr64(shdr + 48, 8u);

    {
        Sha256 sha;

        sha.init();
        sha.update(image, sig_off);
        sha.finish(digest);
        hmac_sha256(key, NK_SECUREKERNEL_DEFAULT_KEY_LEN,
                    image, sig_off, image + sig_off, 0u, mac);
    }

    sig = image + sig_off;
    mem_zero(sig, kSignatureSize);
    mem_copy(sig, (const uint8_t *)NK_SECUREKERNEL_SIG_MAGIC, 8u);
    wr32(sig + kSigVersionOff, NK_SECUREKERNEL_SIG_VERSION);
    wr32(sig + kSigFlagsOff, 0u);
    wr32(sig + kSigImageSizeOff, sig_off);
    mem_copy(sig + kSigHashOff, digest, 32u);
    mem_copy(sig + kSigMacOff, mac, 32u);

    *out_size = result_size;
    return 0;
}

} /* namespace nksecure */
