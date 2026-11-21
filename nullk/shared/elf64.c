/*
 * null_kernel ELF64 loader - little-endian ET_EXEC images with PT_LOAD
 * segments only. Every field is read byte-wise so unaligned images (e.g. one
 * downloaded over UART into an arbitrary buffer) are safe to parse.
 */
#include "nullk/elf64.h"

#include <stddef.h>

/* ELF64 header/program-header offsets. */
#define NK_EHDR_SIZE     64u
#define NK_PHDR_SIZE     56u
#define NK_E_TYPE_OFF    16u
#define NK_E_MACHINE_OFF 18u
#define NK_E_ENTRY_OFF   24u
#define NK_E_PHOFF_OFF   32u
#define NK_E_PHENTSIZE_OFF 54u
#define NK_E_PHNUM_OFF   56u

#define NK_P_TYPE_OFF    0u
#define NK_P_OFFSET_OFF  8u
#define NK_P_VADDR_OFF   16u
#define NK_P_FILESZ_OFF  32u
#define NK_P_MEMSZ_OFF   40u

/* Local error codes (the public API only promises negative on failure). */
enum {
    NK_ELF_ERR_ARG = -1,
    NK_ELF_ERR_FORMAT = -2,
    NK_ELF_ERR_BOUNDS = -3,
    NK_ELF_ERR_CAPACITY = -4
};

/* ------------------------------------------------------------------ */
/* Local helpers.                                                      */
/* ------------------------------------------------------------------ */

static void nk_memcpy(void *dst, const void *src, uint32_t n)
{
    uint8_t *d = (uint8_t *)dst;
    const uint8_t *s = (const uint8_t *)src;
    uint32_t i;

    for (i = 0u; i < n; i++) {
        d[i] = s[i];
    }
}

static void nk_memset(void *dst, int value, uint32_t n)
{
    uint8_t *d = (uint8_t *)dst;
    uint32_t i;

    for (i = 0u; i < n; i++) {
        d[i] = (uint8_t)value;
    }
}

static uint16_t nk_rd16(const uint8_t *p)
{
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

static uint32_t nk_rd32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static uint64_t nk_rd64(const uint8_t *p)
{
    return (uint64_t)nk_rd32(p) | ((uint64_t)nk_rd32(p + 4) << 32);
}

int nk_elf64_check_range(uint64_t base, uint64_t len, uint64_t limit)
{
    if (base > limit) {
        return -1;
    }
    if (len > (limit - base)) {
        return -1;
    }
    return 0;
}

int nk_elf64_probe(const void *data, uint32_t size, nk_elf64_info_t *out)
{
    const uint8_t *image = (const uint8_t *)data;
    uint64_t phoff;
    uint16_t phentsize;
    uint16_t phnum;
    uint32_t i;
    uint32_t load_count = 0u;
    int have_range = 0;
    nk_elf64_info_t info;

    if ((image == (const uint8_t *)0) || (out == (nk_elf64_info_t *)0) ||
        (size < NK_EHDR_SIZE)) {
        return NK_ELF_ERR_ARG;
    }
    if ((image[0] != NK_ELF64_MAGIC0) || (image[1] != 'E') ||
        (image[2] != 'L') || (image[3] != 'F')) {
        return NK_ELF_ERR_FORMAT;
    }
    if ((image[4] != NK_ELF64_CLASS64) || (image[5] != NK_ELF64_DATA_LSB)) {
        return NK_ELF_ERR_FORMAT;
    }
    if (nk_rd16(image + NK_E_TYPE_OFF) != NK_ELF64_ET_EXEC) {
        return NK_ELF_ERR_FORMAT;
    }
    if (nk_rd16(image + NK_E_MACHINE_OFF) != NK_ELF64_EM_X86_64) {
        return NK_ELF_ERR_FORMAT;
    }

    phoff = nk_rd64(image + NK_E_PHOFF_OFF);
    phentsize = nk_rd16(image + NK_E_PHENTSIZE_OFF);
    phnum = nk_rd16(image + NK_E_PHNUM_OFF);
    if ((phentsize != NK_PHDR_SIZE) || (phnum == 0u)) {
        return NK_ELF_ERR_FORMAT;
    }
    if (nk_elf64_check_range(phoff, (uint64_t)phnum * (uint64_t)phentsize,
                             (uint64_t)size) != 0) {
        return NK_ELF_ERR_BOUNDS;
    }

    nk_memset(&info, 0, (uint32_t)sizeof(info));
    info.machine = nk_rd16(image + NK_E_MACHINE_OFF);
    info.type = nk_rd16(image + NK_E_TYPE_OFF);
    info.entry = nk_rd64(image + NK_E_ENTRY_OFF);

    for (i = 0u; i < phnum; i++) {
        const uint8_t *ph = image + (uint32_t)(phoff + (uint64_t)i *
                                                        (uint64_t)phentsize);
        uint32_t p_type = nk_rd32(ph + NK_P_TYPE_OFF);
        uint64_t p_offset = nk_rd64(ph + NK_P_OFFSET_OFF);
        uint64_t p_vaddr = nk_rd64(ph + NK_P_VADDR_OFF);
        uint64_t p_filesz = nk_rd64(ph + NK_P_FILESZ_OFF);
        uint64_t p_memsz = nk_rd64(ph + NK_P_MEMSZ_OFF);
        uint64_t vend;

        if (p_type != NK_ELF64_PT_LOAD) {
            continue;
        }
        if (p_filesz > p_memsz) {
            return NK_ELF_ERR_FORMAT;
        }
        if (nk_elf64_check_range(p_offset, p_filesz, (uint64_t)size) != 0) {
            return NK_ELF_ERR_BOUNDS;
        }
        if (p_vaddr > UINT64_MAX - p_memsz) {
            return NK_ELF_ERR_BOUNDS;
        }
        vend = p_vaddr + p_memsz;
        if (!have_range) {
            info.vaddr_lo = p_vaddr;
            info.vaddr_hi = vend;
            have_range = 1;
        } else {
            if (p_vaddr < info.vaddr_lo) {
                info.vaddr_lo = p_vaddr;
            }
            if (vend > info.vaddr_hi) {
                info.vaddr_hi = vend;
            }
        }
        info.filesz_total += p_filesz;
        info.memsz_total += p_memsz;
        if (info.filesz_total < p_filesz) {
            return NK_ELF_ERR_BOUNDS;
        }
        if (info.memsz_total < p_memsz) {
            return NK_ELF_ERR_BOUNDS;
        }
        load_count++;
    }

    if (load_count == 0u) {
        return NK_ELF_ERR_FORMAT;
    }
    if (info.vaddr_hi < info.vaddr_lo) {
        return NK_ELF_ERR_BOUNDS;
    }
    info.segment_count = load_count;
    *out = info;
    return 0;
}

int nk_elf64_load(const void *data, uint32_t size, void *dest,
                  uint64_t dest_cap, uint64_t *entry_out)
{
    const uint8_t *image = (const uint8_t *)data;
    uint8_t *target = (uint8_t *)dest;
    nk_elf64_info_t info;
    uint64_t span;
    uint64_t phoff;
    uint16_t phentsize;
    uint16_t phnum;
    uint32_t i;
    int status;

    status = nk_elf64_probe(data, size, &info);
    if (status != 0) {
        return status;
    }
    span = info.vaddr_hi - info.vaddr_lo;
    if (span > dest_cap) {
        return NK_ELF_ERR_CAPACITY;
    }
    if ((span != 0u) && (target == (uint8_t *)0)) {
        return NK_ELF_ERR_ARG;
    }

    phoff = nk_rd64(image + NK_E_PHOFF_OFF);
    phentsize = nk_rd16(image + NK_E_PHENTSIZE_OFF);
    phnum = nk_rd16(image + NK_E_PHNUM_OFF);

    for (i = 0u; i < phnum; i++) {
        const uint8_t *ph = image + (uint32_t)(phoff + (uint64_t)i *
                                                        (uint64_t)phentsize);
        uint64_t p_offset;
        uint64_t p_vaddr;
        uint64_t p_filesz;
        uint64_t p_memsz;
        uint64_t dst_off;
        uint64_t bss;

        if (nk_rd32(ph + NK_P_TYPE_OFF) != NK_ELF64_PT_LOAD) {
            continue;
        }
        p_offset = nk_rd64(ph + NK_P_OFFSET_OFF);
        p_vaddr = nk_rd64(ph + NK_P_VADDR_OFF);
        p_filesz = nk_rd64(ph + NK_P_FILESZ_OFF);
        p_memsz = nk_rd64(ph + NK_P_MEMSZ_OFF);
        if (p_vaddr < info.vaddr_lo) {
            return NK_ELF_ERR_BOUNDS;
        }
        dst_off = p_vaddr - info.vaddr_lo;
        if ((p_filesz != 0u) &&
            (nk_elf64_check_range(dst_off, p_filesz, dest_cap) != 0)) {
            return NK_ELF_ERR_CAPACITY;
        }
        bss = p_memsz - p_filesz;
        if ((bss != 0u) &&
            (nk_elf64_check_range(dst_off + p_filesz, bss, dest_cap) != 0)) {
            return NK_ELF_ERR_CAPACITY;
        }
        nk_memcpy(target + (uint32_t)dst_off, image + (uint32_t)p_offset,
                  (uint32_t)p_filesz);
        nk_memset(target + (uint32_t)(dst_off + p_filesz), 0, (uint32_t)bss);
    }

    if (entry_out != (uint64_t *)0) {
        *entry_out = info.entry;
    }
    return 0;
}
