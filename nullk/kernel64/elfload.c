/*
 * Tiny AMD64 kernel core - ELF64 user image loader helpers.
 *
 * nk64_load_user_elf() validates an ET_EXEC image, stage-loads it through a
 * private kernel virtual window and maps the resulting physical frames into a
 * freshly created user CR3. Mapping the same frames avoids a second copy and
 * keeps the privileged image small. The frame list is static because there is
 * no allocator in ring 0 and loading is serialised by the boot/exec path.
 */
#include <stdint.h>

/* kernel64.h contains a nested-comment typo we do not own. */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wcomment"
#include "nullk/kernel64.h"
#pragma GCC diagnostic pop
#include "nullk/abi.h"
#include "nullk/elf64.h"

#define NK64_ELF_TEMP_VA   0xffffffff40000000ULL
#define NK64_ELF_MAX_SPAN  0x00800000ULL          /* 8 MiB per user image */
#define NK64_ELF_MAX_PAGES (NK64_ELF_MAX_SPAN / 4096ULL)

static uint64_t g_frames[(uint32_t)NK64_ELF_MAX_PAGES];

/* Read CR3 without depending on an arch helper that may not be exported. */
static uint64_t nk64_read_cr3_local(void)
{
    uint64_t cr3;

    __asm__ volatile("mov %%cr3, %0" : "=r"(cr3));
    return cr3;
}

/*
 * Conservative user pointer check: the range must lie inside the fixed user
 * arena and must not wrap. The 4 KiB-per-syscall length cap is applied by the
 * syscall layer, not here.
 */
int nk64_check_user_ptr(uint64_t vaddr, uint64_t len)
{
    uint64_t arena = NK64_USER_STACK_TOP - NK64_USER_BASE;

    if (vaddr < NK64_USER_BASE) {
        return 0;
    }
    if (len > arena) {
        return 0;
    }
    if (vaddr > NK64_USER_STACK_TOP - len) {
        return 0;
    }
    return 1;
}

/*
 * Map a validated ELF into the supplied user address space.
 * Returns NK_STATUS_OK on success.
 */
nk_status_t nk64_load_user_elf(const uint8_t *elf, uint32_t size, uint64_t cr3)
{
    nk_elf64_info_t info;
    uint64_t span;
    uint64_t pages;
    uint64_t kernel_cr3;
    uint64_t entry = 0u;
    uint64_t i;

    if (elf == (const uint8_t *)0 || size == 0u || cr3 == 0u) {
        return NK_STATUS_INVALID_ARGUMENT;
    }
    if (nk_elf64_probe(elf, size, &info) != 0) {
        return NK_STATUS_INVALID_ARGUMENT;
    }
    if (info.machine != NK_ELF64_EM_X86_64 ||
        info.type != NK_ELF64_ET_EXEC || info.segment_count == 0u) {
        return NK_STATUS_INVALID_ARGUMENT;
    }
    if (info.vaddr_lo >= info.vaddr_hi) {
        return NK_STATUS_INVALID_ARGUMENT;
    }
    if (info.vaddr_lo < NK64_USER_BASE ||
        info.vaddr_hi > NK64_USER_STACK_TOP) {
        return NK_STATUS_INVALID_ARGUMENT;
    }

    span = info.vaddr_hi - info.vaddr_lo;
    if (span == 0u || span > NK64_ELF_MAX_SPAN) {
        return NK_STATUS_OUT_OF_MEMORY;
    }
    pages = (span + 4095u) / 4096u;
    if (pages > NK64_ELF_MAX_PAGES) {
        return NK_STATUS_OUT_OF_MEMORY;
    }

    kernel_cr3 = nk64_read_cr3_local();

    for (i = 0u; i < pages; i++) {
        uint64_t phys = nk64_pmm_alloc_frame();

        if (phys == 0u) {
            uint64_t j;

            for (j = 0u; j < i; j++) {
                nk64_pmm_free_frame(g_frames[j]);
            }
            return NK_STATUS_OUT_OF_MEMORY;
        }
        g_frames[i] = phys;
        nk64_vmm_map(kernel_cr3, NK64_ELF_TEMP_VA + i * 4096u, phys, 4096u,
                     NK64_PAGE_KERNEL);
    }

    /* nk_elf64_load() zero-fills the .bss tail of the image span. */
    if (nk_elf64_load(elf, size, (void *)(uintptr_t)NK64_ELF_TEMP_VA,
                      span, &entry) != 0) {
        uint64_t j;

        for (j = 0u; j < pages; j++) {
            nk64_pmm_free_frame(g_frames[j]);
        }
        return NK_STATUS_INVALID_ARGUMENT;
    }
    (void)entry;

    /* Executable user pages: user + writable, not NX (code lives here). */
    for (i = 0u; i < pages; i++) {
        nk64_vmm_map(cr3, info.vaddr_lo + i * 4096u, g_frames[i], 4096u,
                     NK64_PAGE_PRESENT | NK64_PAGE_RW | NK64_PAGE_USER);
    }
    return NK_STATUS_OK;
}
