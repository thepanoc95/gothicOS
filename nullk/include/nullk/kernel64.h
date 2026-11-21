/*
 * Internal interfaces for the tiny AMD64 kernel.
 *
 * Layout: Limine maps the kernel at >= 0xffffffff80000000 and gives us an
 * HHDM offset. The QEMU test profile has 16 MiB of RAM; all static kernel
 * arrays are sized for that target so there is no hidden dynamic growth.
 */
#ifndef NULLK_KERNEL64_H
#define NULLK_KERNEL64_H

#include <stdint.h>
#include "abi64.h"
#include "limine.h"

#define NK64_KERNEL_VBASE 0xffffffff80000000ULL
#define NK64_USER_BASE 0x0000000000400000ULL
#define NK64_USER_HEAP 0x0000000050000000ULL
#define NK64_SANDBOX_BASE 0x0000000060000000ULL
#define NK64_SANDBOX_SIZE 0x00100000ULL  /* 1 MiB window inside 16 MiB RAM */
#define NK64_FB_VBASE 0x0000000040000000ULL /* UI server's claimed FB window */
#define NK64_USER_STACK_TOP 0x000000007fff0000ULL
#define NK64_TEST_RAM 0x01000000ULL      /* 16 MiB QEMU test memory */
#define NK64_STACK_PAGES 4u
#define NK64_USER_STACK_PAGES 16u
#define NK64_MAX_BREAKS 16u

enum {
    NK64_RUNLEVEL_RESET = 0,
    NK64_RUNLEVEL_PLATFORM = 1,
    NK64_RUNLEVEL_MEM = 2,
    NK64_RUNLEVEL_IRQ = 3,
    NK64_RUNLEVEL_DRIVERS = 4,
    NK64_RUNLEVEL_TASKS = 5,
    NK64_RUNLEVEL_SHELL = 6,
    NK64_RUNLEVEL_READY = 7
};

/* Boot state gathered from Limine responses. */
typedef struct {
    uint64_t hhdm;
    uint64_t kernel_phys;
    uint64_t kernel_virt;
    uint64_t mem_total;
    uint64_t rsdp_phys;
    uint64_t initrd_phys;
    uint64_t initrd_size;
    uint32_t module_count;
    uint8_t cmdline_present;
    uint8_t rsdp_present;
    nk64_fbinfo_t fb;
} nk64_bootinfo_t;

extern nk64_bootinfo_t nk64_boot;
extern volatile uint32_t nk64_ticks;
extern volatile uint32_t nk64_runlevel;

/* boot.c / main.c */
void nk64_entry(void) __attribute__((noreturn));
void nk64_main(void);
void nk64_boot_fb_init(void);
void nk64_panic(const char *msg) __attribute__((noreturn));

/* arch helpers implemented by the AMD64 architecture layer */
void nk64_arch_init(void);
void nk64_uart_init(void);
void nk64_uart_putc(char c);
void nk64_uart_puts(const char *text, uint32_t len);
int nk64_uart_getc(void);              /* -1 when no byte available */
uint64_t nk64_rdtsc(void);
void nk64_outb(uint16_t port, uint8_t value);
uint8_t nk64_inb(uint16_t port);
void nk64_hlt(void);
void nk64_reboot(void);

/* PS/2 keyboard + mouse input, exposed to userspace through INPUT_READ. */
void nk64_input_init(void);
void nk64_keyboard_handler(void);
void nk64_mouse_handler(void);
uint32_t nk64_input_read(nk64_input_event_t *out, uint32_t max);

/* Paging + physical frames. */
void nk64_pmm_init(const struct limine_memmap_response *mmap, uint64_t hhdm);
uint64_t nk64_pmm_alloc_frame(void);
void nk64_pmm_free_frame(uint64_t phys);
void nk64_pmm_reserve(uint64_t phys, uint64_t len);
uint64_t nk64_phys_to_virt(uint64_t phys);
uint64_t nk64_virt_to_phys(uint64_t virt);
void nk64_vmm_init(void);
uint64_t nk64_vmm_create_space(void);
void nk64_vmm_map(uint64_t cr3, uint64_t vaddr, uint64_t phys,
                  uint64_t len, uint64_t flags);
uint64_t nk64_vmm_alloc_user_pages(uint64_t cr3, uint64_t vaddr, uint64_t len);

#define NK64_PAGE_PRESENT (1ULL << 0)
#define NK64_PAGE_RW (1ULL << 1)
#define NK64_PAGE_USER (1ULL << 2)
#define NK64_PAGE_NX (1ULL << 63)
#define NK64_PAGE_KERNEL (NK64_PAGE_PRESENT | NK64_PAGE_RW | NK64_PAGE_NX)
#define NK64_PAGE_USER_RW (NK64_PAGE_PRESENT | NK64_PAGE_RW | NK64_PAGE_USER)

/* Interrupts and timer. */
void nk64_idt_init(void);
void nk64_pic_init(void);
void nk64_pic_unmask(uint8_t irq);
void nk64_timer_init(uint32_t hz);
void nk64_irq_enable(void);
void nk64_irq_disable(void);
void nk64_timer_handler(void);
void nk64_syscall_init(void);

/* Task/context management. */
typedef struct {
    uint64_t rsp;
    uint64_t cr3;
    uint64_t rip;
    uint64_t rflags;
    uint64_t syscall_stack;
    uint64_t user_rsp;
    uint32_t id;
    uint32_t state;
    uint32_t slice_left;
    char name[NK64_NAME_MAX];
} nk64_tcb_t;

void nk64_task_init(void);
int nk64_task_create_elf(const uint8_t *elf, uint32_t size, const char *name);
int nk64_task_create_kernel(void (*entry)(void), const char *name);
void nk64_task_start_first(void) __attribute__((noreturn));
void nk64_task_yield(void);
void nk64_sched_tick(void);
int nk64_task_alive(int id);

/* Low-level context helpers (context.S / idt.c). */
void nk64_context_switch(uint64_t *old_rsp, uint64_t new_rsp);
void nk64_tss_set_rsp0(uint64_t rsp0);
void nk64_arch_idle(void);

/* Userspace entry/return trampoline (context.S). */
void nk64_usermode_jump(uint64_t rsp, uint64_t rip, uint64_t cr3)
    __attribute__((noreturn));

/* Kernel-mode emergency shell (never returns). */
void nk64_fallback_shell(void) __attribute__((noreturn));

/* Syscall dispatcher. */
uint64_t nk64_dispatch_syscall(uint64_t n, uint64_t a0, uint64_t a1,
                               uint64_t a2, uint64_t a3, uint64_t a4,
                               uint64_t a5);

#endif /* NULLK_KERNEL64_H */
