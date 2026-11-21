/*
 * Limine request block and AMD64 kernel entry point.
 *
 * Limine hands us a long-mode CPU with the higher-half kernel and its HHDM
 * already mapped. We only collect the responses, publish them in nk64_boot,
 * bring up the frame allocator and kernel page tables, then enter the portable
 * kernel. No real-mode or long-mode switch is performed here.
 */

#include <stddef.h>
#include <stdint.h>

/* kernel64.h contains a nested-comment typo in a libc-free header we do not own. */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wcomment"
#include "nullk/kernel64.h"
#pragma GCC diagnostic pop
#include "nullk/printk.h"

/*
 * Limine locates the request table via the .limine_requests section; the two
 * markers are emitted into their own input sections and the linker script
 * brackets the requests with them in the final ELF.
 */
__attribute__((used, section(".limine_requests_start")))
volatile uint64_t limine_requests_start_marker[] = LIMINE_REQUESTS_START_MARKER;

__attribute__((used, section(".limine_requests")))
volatile struct limine_base_revision limine_base_revision[] = {
    /* rev 4 maps ACPI/reserved regions in the HHDM and returns RSDP virtual. */
    { .id = LIMINE_BASE_REVISION(4) }
};

__attribute__((used, section(".limine_requests")))
volatile struct limine_stack_size_request limine_stack_size_request = {
    .id = LIMINE_STACK_SIZE_REQUEST_ID,
    .revision = 0,
    .response = NULL,
    .stack_size = 64u * 1024u
};

__attribute__((used, section(".limine_requests")))
volatile struct limine_hhdm_request limine_hhdm_request = {
    .id = LIMINE_HHDM_REQUEST_ID,
    .revision = 0,
    .response = NULL
};

__attribute__((used, section(".limine_requests")))
volatile struct limine_memmap_request limine_memmap_request = {
    .id = LIMINE_MEMMAP_REQUEST_ID,
    .revision = 0,
    .response = NULL
};

__attribute__((used, section(".limine_requests")))
volatile struct limine_module_request limine_module_request = {
    .id = LIMINE_MODULE_REQUEST_ID,
    .revision = 0,
    .response = NULL,
    .internal_module_count = 0,
    .internal_modules = NULL
};

__attribute__((used, section(".limine_requests")))
volatile struct limine_rsdp_request limine_rsdp_request = {
    .id = LIMINE_RSDP_REQUEST_ID,
    .revision = 0,
    .response = NULL
};

__attribute__((used, section(".limine_requests")))
volatile struct limine_kernel_address_request limine_kernel_address_request = {
    .id = LIMINE_KERNEL_ADDRESS_REQUEST_ID,
    .revision = 0,
    .response = NULL
};

__attribute__((used, section(".limine_requests")))
volatile struct limine_entry_point_request limine_entry_point_request = {
    .id = LIMINE_ENTRY_POINT_REQUEST_ID,
    .revision = 0,
    .response = NULL,
    .entry = nk64_entry
};

__attribute__((used, section(".limine_requests")))
volatile struct limine_executable_cmdline_request limine_cmdline_request = {
    .id = LIMINE_EXECUTABLE_CMDLINE_REQUEST_ID,
    .revision = 0,
    .response = NULL
};

__attribute__((used, section(".limine_requests")))
volatile struct limine_framebuffer_request limine_framebuffer_request = {
    .id = LIMINE_FRAMEBUFFER_REQUEST_ID,
    .revision = 0,
    .response = NULL
};

__attribute__((used, section(".limine_requests_end")))
volatile uint64_t limine_requests_end_marker[] = LIMINE_REQUESTS_END_MARKER;

void nk64_entry(void)
{
    uint64_t i;
    uint64_t total = 0;

    /* Limine may enter with IF set; privileged init must be uninterruptible. */
    __asm__ volatile("cli" : : : "memory");

    /*
     * Limine leaves CR4.OSFXSR clear, but the compiler emits SSE/SSE2 for
     * ordinary 64-bit struct copies. Without these bits any XMM instruction
     * raises #UD; with no IDT installed yet that becomes a triple fault.
     */
    {
        uint64_t cr4;
        __asm__ volatile("mov %%cr4, %0" : "=r"(cr4));
        cr4 |= (1ULL << 9) | (1ULL << 10); /* OSFXSR | OSXMMEXCPT */
        __asm__ volatile("mov %0, %%cr4" : : "r"(cr4) : "memory");
    }

    /* Make early panic diagnostics visible even before arch_init() runs. */
    nk64_uart_init();
    nk_printk_init(nk64_uart_puts);

    /*
     * The header's LIMINE_BASE_REVISION_SUPPORTED() macro indexes a uint64_t[]
     * while this tag is a struct limine_base_revision[]; check both the field
     * the trimmed header uses (id[2]) and the one upstream Limine clears
     * (id[0]) so either bootloader encoding is accepted.
     */
    if (limine_base_revision[0].id[2] != 0u &&
        limine_base_revision[0].id[0] != 0u) {
        nk64_panic("limine: unsupported base revision");
    }

    if (limine_hhdm_request.response == NULL ||
        limine_memmap_request.response == NULL ||
        limine_module_request.response == NULL ||
        limine_kernel_address_request.response == NULL) {
        nk64_panic("limine: required response missing");
    }

    nk64_boot.hhdm = limine_hhdm_request.response->offset;
    nk64_boot.kernel_phys = limine_kernel_address_request.response->physical_base;
    nk64_boot.kernel_virt = limine_kernel_address_request.response->virtual_base;

    if (limine_memmap_request.response->entries != NULL) {
        for (i = 0; i < limine_memmap_request.response->entry_count; i++) {
            struct limine_memmap_entry *e = limine_memmap_request.response->entries[i];
            uint64_t end;

            if (e == NULL || e->type != LIMINE_MEMMAP_USABLE) {
                continue;
            }
            end = e->base + e->length;
            if (end > NK64_TEST_RAM) {
                end = NK64_TEST_RAM;
            }
            if (end > e->base) {
                total += end - e->base;
            }
        }
    }
    nk64_boot.mem_total = total;

    if (limine_module_request.response->module_count != 0u &&
        limine_module_request.response->modules != NULL) {
        nk64_boot.module_count =
            (uint32_t)limine_module_request.response->module_count;
        nk64_boot.initrd_phys = (uint64_t)(uintptr_t)
            limine_module_request.response->modules[0]->address;
        nk64_boot.initrd_size = limine_module_request.response->modules[0]->size;
    }

    if (limine_rsdp_request.response != NULL &&
        limine_rsdp_request.response->address != NULL) {
        nk64_boot.rsdp_present = 1u;
        nk64_boot.rsdp_phys = (uint64_t)(uintptr_t)
            limine_rsdp_request.response->address;
    }

    if (limine_cmdline_request.response != NULL &&
        limine_cmdline_request.response->cmdline != NULL) {
        nk64_boot.cmdline_present = 1u;
    }

    /* Both helpers are idempotent: the portable kernel may call them again. */
    nk64_pmm_init(limine_memmap_request.response, nk64_boot.hhdm);
    nk64_boot_fb_init();
    nk64_vmm_init();

    nk64_main();

    /* nk64_main() is not expected to return, but never leave privileged mode. */
    for (;;) {
        nk64_hlt();
    }
}
