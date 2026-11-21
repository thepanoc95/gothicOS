/*
 * Minimal ELF64 loader used for userspace binaries (init, shell, USB/UART
 * downloaded executables). It only understands little-endian ET_EXEC images
 * with PT_LOAD segments; dynamic linking is explicitly out of scope.
 */
#ifndef NULLK_ELF64_H
#define NULLK_ELF64_H

#include <stdint.h>

#define NK_ELF64_MAGIC0 0x7f
#define NK_ELF64_CLASS64 2u
#define NK_ELF64_DATA_LSB 1u
#define NK_ELF64_ET_EXEC 2u
#define NK_ELF64_EM_X86_64 62u
#define NK_ELF64_PT_LOAD 1u

/* Machine independent description of an image. */
typedef struct {
    uint16_t machine;
    uint16_t type;
    uint64_t entry;
    uint64_t vaddr_lo;
    uint64_t vaddr_hi;
    uint64_t filesz_total;
    uint64_t memsz_total;
    uint32_t segment_count;
} nk_elf64_info_t;

/* Validate header/program table without touching the destination. */
int nk_elf64_probe(const void *data, uint32_t size, nk_elf64_info_t *out);

/* Load all PT_LOAD segments into dest at (p_vaddr - vaddr_lo), zero-filling
 * the .bss portion. Returns 0 on success; dest_cap must be >= image span. */
int nk_elf64_load(const void *data, uint32_t size, void *dest,
                  uint64_t dest_cap, uint64_t *entry_out);

/* Bounds-checked helpers used by the UART downloader. */
int nk_elf64_check_range(uint64_t base, uint64_t len, uint64_t limit);

#endif /* NULLK_ELF64_H */
