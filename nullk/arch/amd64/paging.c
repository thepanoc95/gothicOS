/*
 * Tiny physical frame allocator and 4-level paging for the AMD64 port.
 *
 * The QEMU test profile has 16 MiB of RAM, so the whole free/used bitmap is a
 * static 4096-byte array. All page-table writes go through the Limine HHDM
 * (physical + hhdm); the kernel image, modules and non-usable memory regions
 * are never handed out. VMM state is a small set of static counters: no heap,
 * no dependence on the portable allocator.
 */

#include <stddef.h>
#include <stdint.h>

/* kernel64.h contains a nested-comment typo in a libc-free header we do not own. */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wcomment"
#include "nullk/kernel64.h"
#pragma GCC diagnostic pop

extern nk64_bootinfo_t nk64_boot;
extern volatile struct limine_memmap_request limine_memmap_request;
extern volatile struct limine_module_request limine_module_request;
extern uint8_t _kernel_start[];
extern uint8_t _kernel_end[];

#define NK64_FRAME_SHIFT 12u
#define NK64_FRAME_SIZE (1ULL << NK64_FRAME_SHIFT)
#define NK64_FRAME_MASK (NK64_FRAME_SIZE - 1ULL)
#define NK64_FRAME_COUNT (NK64_TEST_RAM >> NK64_FRAME_SHIFT)
#define NK64_ADDR_MASK 0x000ffffffffff000ULL
#define NK64_PAGE_PS (1ULL << 7)

#define NK64_PT_ENTRIES 512u

static uint8_t nk64_frame_used[NK64_FRAME_COUNT];
static uint64_t nk64_alloc_hint;
static uint64_t nk64_hhdm;
static uint64_t nk64_kernel_cr3;
static uint8_t nk64_pmm_ready;
static uint8_t nk64_vmm_ready;

typedef uint64_t nk64_pte_t;

static uint64_t nk64_align_down(uint64_t value, uint64_t align)
{
    return value & ~(align - 1ULL);
}

static uint64_t nk64_align_up(uint64_t value, uint64_t align)
{
    return (value + align - 1ULL) & ~(align - 1ULL);
}

static uint64_t nk64_kernel_image_size(void)
{
    uint64_t size = (uint64_t)(uintptr_t)(_kernel_end - _kernel_start);

    if (size == 0u || size > NK64_TEST_RAM) {
        size = 8u * 1024u * 1024u;
    }
    return size;
}

/* Limine revision 3 may report module addresses as HHDM virtual addresses. */
static uint64_t nk64_module_phys(uint64_t raw)
{
    if (nk64_hhdm != 0u && raw >= nk64_hhdm &&
        raw - nk64_hhdm < NK64_TEST_RAM) {
        return raw - nk64_hhdm;
    }
    return raw;
}

static void nk64_reserve_range(uint64_t phys, uint64_t len);
static nk64_pte_t *nk64_table(uint64_t table_phys);

/* Keep Limine's live page tables out of the allocator's free pool. */
static void nk64_reserve_table_tree(uint64_t table_phys, uint32_t level)
{
    nk64_pte_t *table;
    uint64_t i;

    if (table_phys == 0u) {
        return;
    }
    nk64_reserve_range(table_phys, NK64_FRAME_SIZE);
    if (level == 0u) {
        return; /* leaf PTEs are data pages, not tables */
    }

    table = nk64_table(table_phys);
    for (i = 0; i < NK64_PT_ENTRIES; i++) {
        uint64_t entry = table[i];

        if ((entry & NK64_PAGE_PRESENT) == 0u) {
            continue;
        }
        /* Large pages at PDPT/PD level are leaves, not child tables. */
        if (level <= 2u && (entry & NK64_PAGE_PS) != 0u) {
            continue;
        }
        nk64_reserve_table_tree(entry & NK64_ADDR_MASK, level - 1u);
    }
}

static void nk64_reserve_range(uint64_t phys, uint64_t len)
{
    uint64_t start;
    uint64_t end;
    uint64_t p;

    if (len == 0u || phys >= NK64_TEST_RAM) {
        return;
    }
    end = phys + len;
    if (end > NK64_TEST_RAM) {
        end = NK64_TEST_RAM;
    }
    start = nk64_align_up(phys, NK64_FRAME_SIZE);
    end = nk64_align_down(end, NK64_FRAME_SIZE);
    for (p = start; p < end; p += NK64_FRAME_SIZE) {
        nk64_frame_used[p >> NK64_FRAME_SHIFT] = 1u;
    }
}

void nk64_pmm_init(const struct limine_memmap_response *mmap, uint64_t hhdm)
{
    uint64_t i;

    if (nk64_pmm_ready) {
        return;
    }
    nk64_hhdm = hhdm;
    if (nk64_hhdm == 0u && nk64_boot.hhdm != 0u) {
        nk64_hhdm = nk64_boot.hhdm;
    }
    nk64_alloc_hint = 0u;

    /* Start with everything reserved, then free usable map entries. */
    for (i = 0; i < NK64_FRAME_COUNT; i++) {
        nk64_frame_used[i] = 1u;
    }

    if (mmap != NULL && mmap->entries != NULL) {
        for (i = 0; i < mmap->entry_count; i++) {
            const struct limine_memmap_entry *e = mmap->entries[i];
            uint64_t start;
            uint64_t end;
            uint64_t p;

            if (e == NULL || e->type != LIMINE_MEMMAP_USABLE) {
                continue;
            }
            start = nk64_align_up(e->base, NK64_FRAME_SIZE);
            end = nk64_align_down(e->base + e->length, NK64_FRAME_SIZE);
            if (end > NK64_TEST_RAM) {
                end = NK64_TEST_RAM;
            }
            for (p = start; p < end; p += NK64_FRAME_SIZE) {
                nk64_frame_used[p >> NK64_FRAME_SHIFT] = 0u;
            }
        }
    }

    /* Frame zero is the legacy BIOS area; never hand it to a client. */
    nk64_frame_used[0] = 1u;
    nk64_reserve_range(nk64_boot.kernel_phys, nk64_kernel_image_size());
    nk64_reserve_range(nk64_module_phys(nk64_boot.initrd_phys),
                       nk64_boot.initrd_size);

    if (limine_module_request.response != NULL &&
        limine_module_request.response->modules != NULL) {
        for (i = 0; i < limine_module_request.response->module_count; i++) {
            const struct limine_file *file = limine_module_request.response->modules[i];
            if (file != NULL) {
                nk64_reserve_range(
                    nk64_module_phys((uint64_t)(uintptr_t)file->address),
                    file->size);
            }
        }
    }

    nk64_pmm_ready = 1u;
}

uint64_t nk64_pmm_alloc_frame(void)
{
    uint64_t i;

    if (!nk64_pmm_ready) {
        return 0u;
    }
    for (i = 0; i < NK64_FRAME_COUNT; i++) {
        uint64_t idx = (nk64_alloc_hint + i) % NK64_FRAME_COUNT;

        if (nk64_frame_used[idx] == 0u) {
            nk64_frame_used[idx] = 1u;
            nk64_alloc_hint = (idx + 1u) % NK64_FRAME_COUNT;
            return idx << NK64_FRAME_SHIFT;
        }
    }
    return 0u;
}

void nk64_pmm_reserve(uint64_t phys, uint64_t len)
{
    if (nk64_pmm_ready) {
        nk64_reserve_range(phys, len);
    }
}

void nk64_pmm_free_frame(uint64_t phys)
{
    uint64_t idx;

    if (!nk64_pmm_ready || phys >= NK64_TEST_RAM ||
        (phys & NK64_FRAME_MASK) != 0u) {
        return;
    }
    idx = phys >> NK64_FRAME_SHIFT;
    nk64_frame_used[idx] = 0u;
    if (idx < nk64_alloc_hint) {
        nk64_alloc_hint = idx;
    }
}

uint64_t nk64_phys_to_virt(uint64_t phys)
{
    return phys + nk64_hhdm;
}

uint64_t nk64_virt_to_phys(uint64_t virt)
{
    uint64_t image = nk64_kernel_image_size();

    if (nk64_boot.kernel_virt != 0u && virt >= nk64_boot.kernel_virt &&
        virt - nk64_boot.kernel_virt < image) {
        return nk64_boot.kernel_phys + (virt - nk64_boot.kernel_virt);
    }
    return virt - nk64_hhdm;
}

static uint64_t nk64_read_cr3(void)
{
    uint64_t value;

    __asm__ volatile("movq %%cr3, %0" : "=r"(value));
    return value;
}

static void nk64_write_cr3(uint64_t value)
{
    __asm__ volatile("movq %0, %%cr3" : : "r"(value) : "memory");
}

static nk64_pte_t *nk64_table(uint64_t table_phys)
{
    return (nk64_pte_t *)(uintptr_t)nk64_phys_to_virt(table_phys);
}

static uint64_t nk64_alloc_table(void)
{
    uint64_t phys = nk64_pmm_alloc_frame();
    nk64_pte_t *table;
    uint64_t i;

    if (phys == 0u) {
        return 0u;
    }
    table = nk64_table(phys);
    for (i = 0; i < NK64_PT_ENTRIES; i++) {
        table[i] = 0u;
    }
    return phys;
}

/* Return the child table address, allocating it when the entry is empty. */
static uint64_t nk64_child_table(uint64_t parent_phys, uint64_t index,
                                 uint64_t flags)
{
    nk64_pte_t *table = nk64_table(parent_phys);
    nk64_pte_t entry = table[index];

    if ((entry & NK64_PAGE_PRESENT) != 0u) {
        if ((entry & NK64_PAGE_PS) != 0u) {
            return 0u; /* a large page is already mapped here */
        }
        return entry & NK64_ADDR_MASK;
    }

    {
        uint64_t child = nk64_alloc_table();

        if (child == 0u) {
            return 0u;
        }
        table[index] = child | NK64_PAGE_PRESENT | NK64_PAGE_RW |
                       (flags & NK64_PAGE_USER);
        return child;
    }
}

void nk64_vmm_init(void)
{
    uint64_t source;
    uint64_t copy;
    uint64_t i;

    if (nk64_vmm_ready) {
        return;
    }
    if (!nk64_pmm_ready) {
        nk64_pmm_init(limine_memmap_request.response, nk64_boot.hhdm);
    }
    if (nk64_hhdm == 0u || !nk64_pmm_ready) {
        return;
    }

    /*
     * Adopt Limine's mappings under our own top-level table. We copy every
     * PML4 entry, so the kernel, HHDM and the boot stack remain untouched;
     * per-task spaces later copy only the upper half.
     */
    source = nk64_read_cr3() & NK64_ADDR_MASK;
    nk64_reserve_table_tree(source, 3u);
    copy = nk64_alloc_table();
    if (copy == 0u) {
        return;
    }
    for (i = 0; i < NK64_PT_ENTRIES; i++) {
        nk64_table(copy)[i] = nk64_table(source)[i];
    }
    nk64_kernel_cr3 = copy;
    nk64_write_cr3(copy);
    nk64_vmm_ready = 1u;
}

uint64_t nk64_vmm_create_space(void)
{
    uint64_t space;
    uint64_t i;

    if (!nk64_vmm_ready) {
        nk64_vmm_init();
    }
    if (nk64_kernel_cr3 == 0u) {
        return 0u;
    }

    space = nk64_alloc_table();
    if (space == 0u) {
        return 0u;
    }
    /* Upper half: HHDM and the higher-half kernel are shared by all tasks. */
    for (i = 256u; i < NK64_PT_ENTRIES; i++) {
        nk64_table(space)[i] = nk64_table(nk64_kernel_cr3)[i];
    }
    return space;
}

void nk64_vmm_map(uint64_t cr3, uint64_t vaddr, uint64_t phys,
                  uint64_t len, uint64_t flags)
{
    uint64_t addr;
    uint64_t paddr;
    uint64_t end;
    uint64_t pml4;

    if (cr3 == 0u || len == 0u || nk64_hhdm == 0u) {
        return;
    }
    addr = nk64_align_down(vaddr, NK64_FRAME_SIZE);
    paddr = nk64_align_down(phys, NK64_FRAME_SIZE);
    end = nk64_align_up(vaddr + len, NK64_FRAME_SIZE);
    pml4 = cr3 & NK64_ADDR_MASK;

    while (addr < end) {
        uint64_t pdpt;
        uint64_t pd;
        uint64_t pt;
        nk64_pte_t *pd_table;

        pdpt = nk64_child_table(pml4, (addr >> 39) & 511u, flags);
        if (pdpt == 0u) {
            return;
        }
        pd = nk64_child_table(pdpt, (addr >> 30) & 511u, flags);
        if (pd == 0u) {
            return;
        }
        pd_table = nk64_table(pd);

        /* A 2 MiB mapping is already present: leave it alone. */
        if ((pd_table[(addr >> 21) & 511u] & NK64_PAGE_PRESENT) != 0u &&
            (pd_table[(addr >> 21) & 511u] & NK64_PAGE_PS) != 0u) {
            paddr += 2u * 1024u * 1024u;
            addr = nk64_align_up(addr + 1u, 2u * 1024u * 1024u);
            continue;
        }

        if ((addr & (2u * 1024u * 1024u - 1u)) == 0u &&
            (paddr & (2u * 1024u * 1024u - 1u)) == 0u &&
            (end - addr) >= 2u * 1024u * 1024u) {
            pd_table[(addr >> 21) & 511u] =
                paddr | flags | NK64_PAGE_PRESENT | NK64_PAGE_PS;
            addr += 2u * 1024u * 1024u;
            paddr += 2u * 1024u * 1024u;
            continue;
        }

        pt = nk64_child_table(pd, (addr >> 21) & 511u, flags);
        if (pt == 0u) {
            return;
        }
        nk64_table(pt)[(addr >> 12) & 511u] =
            paddr | flags | NK64_PAGE_PRESENT;
        addr += NK64_FRAME_SIZE;
        paddr += NK64_FRAME_SIZE;
    }
}

uint64_t nk64_vmm_alloc_user_pages(uint64_t cr3, uint64_t vaddr, uint64_t len)
{
    uint64_t pages = nk64_align_up(len, NK64_FRAME_SIZE) >> NK64_FRAME_SHIFT;
    uint64_t i;
    uint64_t mapped = 0u;

    for (i = 0; i < pages; i++) {
        uint64_t frame = nk64_pmm_alloc_frame();
        uint8_t *target;
        uint64_t j;

        if (frame == 0u) {
            break;
        }
        target = (uint8_t *)(uintptr_t)nk64_phys_to_virt(frame);
        for (j = 0; j < NK64_FRAME_SIZE; j++) {
            target[j] = 0u;
        }
        nk64_vmm_map(cr3, vaddr + i * NK64_FRAME_SIZE, frame,
                     NK64_FRAME_SIZE, NK64_PAGE_USER_RW);
        mapped++;
    }
    return mapped;
}
