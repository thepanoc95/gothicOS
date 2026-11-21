/*
 * Tiny AMD64 kernel core - Limine entry point and boot orchestration.
 *
 * This file owns the small set of global boot variables used by the rest of
 * the privileged image, initialises the fixed-size PMM/VMM and hands control
 * to the userspace init task. If no usable init ELF is present in the initrd
 * the tiny ring-0 fallback shell is entered instead (it never returns).
 *
 * No libc, no heap: the only large object is the static ELF/UART download
 * buffer which is deliberately placed in .bss and reused for every load.
 */
#include <stdint.h>

/* kernel64.h contains a nested-comment typo we do not own. */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wcomment"
#include "nullk/kernel64.h"
#pragma GCC diagnostic pop
#include "nullk/abi.h"
#include "nullk/printk.h"
#include "nullk/vfs.h"
#include "nullk/acpi.h"
#include "nullk/elf64.h"

/* 512 KiB is enough for the test initrd images and matches the UART loader. */
#define NK64_ELF_MAX 0x80000u

/* Shared bounce buffer: declared extern by elfload/uartload/shell/syscall. */
uint8_t nk64_elf_buffer[NK64_ELF_MAX];

nk64_bootinfo_t nk64_boot;
volatile uint32_t nk64_ticks;
volatile uint32_t nk64_runlevel;

/*
 * Limine request symbols. These are emitted by arch/amd64/boot.c; the trimmed
 * limine.h deliberately does not declare them for us. The base revision array
 * is three uint64 words (id[0], id[1], revision) which is layout-compatible
 * with struct limine_base_revision, so the untyped declaration is safe.
 */
extern uint64_t limine_base_revision[];
extern struct limine_hhdm_request limine_hhdm_request;
extern struct limine_memmap_request limine_memmap_request;
extern struct limine_module_request limine_module_request;
extern struct limine_rsdp_request limine_rsdp_request;
extern struct limine_kernel_address_request limine_kernel_address_request;
/* arch/amd64/boot.c names this request `limine_cmdline_request`. */
extern struct limine_executable_cmdline_request limine_cmdline_request;
extern struct limine_framebuffer_request limine_framebuffer_request;

/* Fallback shell lives in fallback_shell.c and never returns. */
void nk64_fallback_shell(void) __attribute__((noreturn));

/* ------------------------------------------------------------------ */
/* Small local helpers (no string.h in the kernel image).              */
/* ------------------------------------------------------------------ */

static void nk64_zero(void *dst, uint32_t len)
{
    uint8_t *p = (uint8_t *)dst;

    while (len-- != 0u) {
        *p++ = 0u;
    }
}

static uint32_t nk64_str_len(const char *s)
{
    uint32_t n = 0u;

    if (s == (const char *)0) {
        return 0u;
    }
    while (s[n] != '\0') {
        n++;
    }
    return n;
}

static int nk64_str_contains(const char *haystack, const char *needle)
{
    uint32_t hlen;
    uint32_t nlen;
    uint32_t i;

    if (haystack == (const char *)0 || needle == (const char *)0) {
        return 0;
    }
    hlen = nk64_str_len(haystack);
    nlen = nk64_str_len(needle);
    if (nlen == 0u || nlen > hlen) {
        return 0;
    }
    for (i = 0u; i <= hlen - nlen; i++) {
        uint32_t j;

        for (j = 0u; j < nlen; j++) {
            if (haystack[i + j] != needle[j]) {
                break;
            }
        }
        if (j == nlen) {
            return 1;
        }
    }
    return 0;
}

static uint64_t nk64_total_ram(const struct limine_memmap_response *mmap)
{
    uint64_t total = 0u;
    uint64_t i;

    if (mmap == (const struct limine_memmap_response *)0) {
        return 0u;
    }
    for (i = 0u; i < mmap->entry_count; i++) {
        const struct limine_memmap_entry *e = mmap->entries[i];

        if (e != (const struct limine_memmap_entry *)0 &&
            e->type == LIMINE_MEMMAP_USABLE) {
            total += e->length;
        }
    }
    if (total > NK64_TEST_RAM) {
        total = NK64_TEST_RAM;
    }
    return total;
}

/*
 * Limine base revision 3 hands modules back as HHDM virtual addresses;
 * older revisions use physical addresses. Rather than trusting the revision
 * alone we accept whichever form falls inside the HHDM window.
 */
static uint64_t nk64_module_address(uint64_t raw)
{
    uint64_t hhdm = nk64_boot.hhdm;
    uint64_t window = nk64_boot.mem_total;

    if (window == 0u) {
        window = NK64_TEST_RAM;
    }
    (void)window;
    /* Limine 12.x reports module/RSDP addresses already in the HHDM. Use the
     * whole higher-half as the test window: usable RAM is smaller than the
     * module region, so bounding by mem_total would add the HHDM twice. */
    if (hhdm != 0u && raw >= hhdm &&
        (raw - hhdm) < 0x0000800000000000ULL) {
        return raw;                 /* already HHDM mapped */
    }
    return nk64_phys_to_virt(raw);  /* physical -> HHDM */
}

static uintptr_t nk64_acpi_addr(uint64_t phys)
{
    return (uintptr_t)nk64_module_address(phys);
}

static const struct limine_file *nk64_find_initrd(void)
{
    const struct limine_module_response *mr = limine_module_request.response;
    const struct limine_file *fallback = (const struct limine_file *)0;
    uint64_t i;

    if (mr == (const struct limine_module_response *)0) {
        return (const struct limine_file *)0;
    }
    for (i = 0u; i < mr->module_count; i++) {
        const struct limine_file *m = mr->modules[i];

        if (m == (const struct limine_file *)0) {
            continue;
        }
        if (fallback == (const struct limine_file *)0) {
            fallback = m;
        }
        if (nk64_str_contains(m->path, "initrd") ||
            nk64_str_contains(m->path, "tar")) {
            return m;
        }
    }
    return fallback;
}

/*
 * arch/amd64/boot.c normally owns nk64_entry() and already performs the PMM
 * and VMM setup. It does not call nk64_arch_init()/nk64_task_init(), so these
 * are performed here exactly once. Keeping this entry weak avoids a duplicate
 * symbol with boot.c while still providing a fallback for alternate builds.
 */
static uint8_t g_early_init_done;

static void nk64_core_early_init(void)
{
    if (g_early_init_done != 0u) {
        return;
    }
    g_early_init_done = 1u;

    /* Initialise task state before enabling the timer. */
    nk64_task_init();
    nk64_arch_init();
}

/*
 * Publish the first Limine framebuffer. boot.c and the weak entry point both
 * call this after the PMM is ready, so the linear buffer can be reserved
 * before any task pages are allocated. Only metadata crosses into ABI v3;
 * mapping it is an explicit FB_MAP request from the UI server.
 */
void nk64_boot_fb_init(void)
{
    const struct limine_framebuffer_response *fb_response;
    const struct limine_framebuffer *fb;
    uint64_t raw;
    uint64_t phys;
    uint64_t bytes;

    fb_response = limine_framebuffer_request.response;
    if (fb_response == (const struct limine_framebuffer_response *)0 ||
        fb_response->framebuffer_count == 0u ||
        fb_response->framebuffers == (struct limine_framebuffer **)0) {
        return;
    }
    fb = fb_response->framebuffers[0];
    if (fb == (const struct limine_framebuffer *)0 || fb->address == NULL ||
        fb->width == 0u || fb->height == 0u || fb->pitch == 0u ||
        (fb->bpp != 24u && fb->bpp != 32u) ||
        fb->width > 8192u || fb->height > 8192u ||
        fb->pitch > (8192u * 4u)) {
        return;
    }

    raw = (uint64_t)(uintptr_t)fb->address;
    phys = raw;
    if (nk64_boot.hhdm != 0u && raw >= nk64_boot.hhdm) {
        phys = raw - nk64_boot.hhdm;
    }
    bytes = fb->pitch * fb->height;

    nk64_boot.fb.phys = phys;
    nk64_boot.fb.width = (uint32_t)fb->width;
    nk64_boot.fb.height = (uint32_t)fb->height;
    nk64_boot.fb.pitch = (uint32_t)fb->pitch;
    nk64_boot.fb.bpp = fb->bpp;
    nk64_boot.fb.red_size = fb->red_mask_size;
    nk64_boot.fb.red_shift = fb->red_mask_shift;
    nk64_boot.fb.green_size = fb->green_mask_size;
    nk64_boot.fb.green_shift = fb->green_mask_shift;
    nk64_boot.fb.blue_size = fb->blue_mask_size;
    nk64_boot.fb.blue_shift = fb->blue_mask_shift;
    nk64_boot.fb.present = 1u;
    nk64_pmm_reserve(phys, bytes);
}

/* ------------------------------------------------------------------ */
/* Public entry points.                                                */
/* ------------------------------------------------------------------ */

__attribute__((weak)) void nk64_entry(void)
{
    const struct limine_hhdm_response *hhdmr;
    struct limine_memmap_response *mmap;
    const struct limine_kernel_address_response *kar;
    const struct limine_rsdp_response *rsdpr;
    const struct limine_executable_cmdline_response *cmdr;
    uint64_t hhdm = 0u;

    /* Limine clears .bss, but do our own bookkeeping explicitly. */
    nk64_zero(&nk64_boot, (uint32_t)sizeof(nk64_boot));
    nk64_ticks = 0u;
    nk64_runlevel = NK64_RUNLEVEL_RESET;

    nk64_uart_init();
    nk_printk_init(nk64_uart_puts);

    if (!LIMINE_BASE_REVISION_SUPPORTED(limine_base_revision)) {
        nk64_panic("limine base revision unsupported");
    }

    hhdmr = limine_hhdm_request.response;
    if (hhdmr != (const struct limine_hhdm_response *)0) {
        hhdm = hhdmr->offset;
    }
    mmap = limine_memmap_request.response;
    if (mmap == (struct limine_memmap_response *)0) {
        nk64_panic("limine memmap response missing");
    }

    nk64_boot.hhdm = hhdm;
    nk64_boot.mem_total = nk64_total_ram(mmap);
    nk64_runlevel = NK64_RUNLEVEL_PLATFORM;

    kar = limine_kernel_address_request.response;
    if (kar != (const struct limine_kernel_address_response *)0) {
        nk64_boot.kernel_phys = kar->physical_base;
        nk64_boot.kernel_virt = kar->virtual_base;
    }

    rsdpr = limine_rsdp_request.response;
    if (rsdpr != (const struct limine_rsdp_response *)0 &&
        rsdpr->address != (void *)0) {
        nk64_boot.rsdp_phys = (uint64_t)rsdpr->address;
        nk64_boot.rsdp_present = 1u;
    }

    cmdr = limine_cmdline_request.response;
    if (cmdr != (const struct limine_executable_cmdline_response *)0 &&
        cmdr->cmdline != (char *)0 && cmdr->cmdline[0] != '\0') {
        nk64_boot.cmdline_present = 1u;
    }

    {
        const struct limine_file *mod = nk64_find_initrd();

        if (mod != (const struct limine_file *)0) {
            nk64_boot.initrd_phys = (uint64_t)mod->address;
            nk64_boot.initrd_size = mod->size;
        }
    }
    if (limine_module_request.response !=
        (struct limine_module_response *)0) {
        nk64_boot.module_count =
            (uint32_t)limine_module_request.response->module_count;
    }

    nk64_pmm_init(mmap, hhdm);
    nk64_runlevel = NK64_RUNLEVEL_MEM;
    nk64_boot_fb_init();
    nk64_vmm_init();

    nk64_core_early_init();
    nk64_runlevel = NK64_RUNLEVEL_IRQ;
    nk64_runlevel = NK64_RUNLEVEL_TASKS;

    nk64_main();

    /* nk64_main() normally enters userspace or the fallback shell. */
    for (;;) {
        nk64_hlt();
    }
}

void nk64_main(void)
{
    nk_acpi_info_t acpi;
    const struct limine_file *mod;
    uint32_t init_len = 0u;
    int task_id;

    /* boot.c calls this directly without going through arch/task init. */
    nk64_core_early_init();

    nk_printk_init(nk64_uart_puts);
    nk_printk_set_level(NK_KERN_INFO);
    pr_info("null_kernel/AMD64: tiny core, ABI v3, %u KiB RAM\n",
            (uint32_t)(nk64_boot.mem_total / 1024u));
    pr_info("null_kernel: hhdm=0x%08x%08x kernel_phys=0x%08x%08x\n",
            (uint32_t)(nk64_boot.hhdm >> 32), (uint32_t)nk64_boot.hhdm,
            (uint32_t)(nk64_boot.kernel_phys >> 32),
            (uint32_t)nk64_boot.kernel_phys);

    nk_vfs_init();
    nk64_runlevel = NK64_RUNLEVEL_DRIVERS;

    mod = nk64_find_initrd();
    if (mod != (const struct limine_file *)0 && mod->size != 0u) {
        uint64_t addr = nk64_module_address((uint64_t)mod->address);
        uint32_t size = (mod->size > 0xffffffffULL)
                            ? 0xffffffffu : (uint32_t)mod->size;
        nk_status_t st;

        pr_info("null_kernel: initrd module size=%u bytes\n", size);
        st = nk_vfs_load_tar((const uint8_t *)addr, size);
        if (st != NK_STATUS_OK) {
            pr_warn("null_kernel: initrd load failed (status %u)\n",
                    (uint32_t)st);
        }
    } else {
        pr_warn("null_kernel: no initrd module found\n");
    }

    nk64_zero(&acpi, (uint32_t)sizeof(acpi));
    if (nk64_boot.rsdp_present != 0u) {
        /*
         * Limine base revision 4 returns the RSDP in the HHDM, but addresses
         * inside ACPI tables are physical. Normalise the root pointer here and
         * let the walker translate every table address through the same map.
         */
        int rc = nk_acpi_parse_with_map(
            (const uint8_t *)nk64_module_address(nk64_boot.rsdp_phys), &acpi,
            nk64_acpi_addr);

        if (rc == 0) {
            pr_info("null_kernel: ACPI cpus=%u lapic=0x%08x%08x "
                    "ioapic=0x%08x%08x gsi_base=%u\n",
                    (uint32_t)acpi.cpu_count,
                    (uint32_t)(acpi.lapic_addr >> 32),
                    (uint32_t)acpi.lapic_addr,
                    (uint32_t)(acpi.ioapic_addr >> 32),
                    (uint32_t)acpi.ioapic_addr,
                    (uint32_t)acpi.ioapic_gsi_base);
        } else {
            pr_warn("null_kernel: ACPI parse failed (%d)\n", rc);
        }
    }

    /* Try the normal userspace init first; fall back to ring-0 shell. */
    {
        nk_status_t st = nk_vfs_read_file("@/data/system/init.elf", 0u,
                                          nk64_elf_buffer, NK64_ELF_MAX,
                                          &init_len);

        if (st == NK_STATUS_OK && init_len != 0u) {
            task_id = nk64_task_create_elf(nk64_elf_buffer, init_len,
                                            "init");
        } else {
            pr_warn("null_kernel: init.elf unavailable (status %u)\n",
                    (uint32_t)st);
            task_id = -1;
        }
    }

    if (task_id >= 0) {
        pr_info("null_kernel: starting userspace task %d\n", task_id);
        nk64_runlevel = NK64_RUNLEVEL_READY;
        nk64_task_start_first();   /* noreturn */
    }

    pr_warn("null_kernel: entering fallback shell\n");
    nk64_runlevel = NK64_RUNLEVEL_SHELL;
    nk64_fallback_shell();         /* noreturn */
}

void nk64_panic(const char *msg)
{
    nk64_irq_disable();
    pr_emerg("null_kernel PANIC: %s\n",
             (msg != (const char *)0) ? msg : "(null)");
    for (;;) {
        nk64_hlt();
    }
}
