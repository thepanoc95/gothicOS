/*
 * AMD64 architecture glue.
 *
 * nk64_arch_init() brings up the privileged CPU state in the order the rest of
 * the kernel expects: console first, then exception/IRQ handling, then the
 * syscall MSRs and the periodic timer. Interrupts stay off until the very end.
 */

#include <stdint.h>

/* kernel64.h contains a nested-comment typo in a libc-free header we do not own. */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wcomment"
#include "nullk/kernel64.h"
#pragma GCC diagnostic pop

/* MSRs used by the syscall fast path. */
#define NK64_MSR_EFER 0xc0000080u
#define NK64_MSR_STAR 0xc0000081u
#define NK64_MSR_LSTAR 0xc0000082u
#define NK64_MSR_FMASK 0xc0000084u

#define NK64_EFER_SCE (1ULL << 0)

/* GDT selectors installed by nk64_idt_init(). */
#define NK64_KERNEL_CS 0x08u
#define NK64_USER_CS_BASE 0x13u /* SYSRET adds 16 -> 0x23 */

extern void nk64_syscall_entry(void);

static uint64_t nk64_msr_read(uint32_t msr)
{
    uint32_t lo;
    uint32_t hi;

    __asm__ volatile("rdmsr" : "=a"(lo), "=d"(hi) : "c"(msr));
    return ((uint64_t)hi << 32) | (uint64_t)lo;
}

static void nk64_msr_write(uint32_t msr, uint64_t value)
{
    uint32_t lo = (uint32_t)value;
    uint32_t hi = (uint32_t)(value >> 32);

    __asm__ volatile("wrmsr" : : "c"(msr), "a"(lo), "d"(hi));
}

void nk64_syscall_init(void)
{
    uint64_t star = ((uint64_t)NK64_USER_CS_BASE << 48) |
                    ((uint64_t)NK64_KERNEL_CS << 32);

    nk64_msr_write(NK64_MSR_STAR, star);
    nk64_msr_write(NK64_MSR_LSTAR, (uint64_t)(uintptr_t)nk64_syscall_entry);
    /* Clear IF (and the direction flag) on syscall entry. */
    nk64_msr_write(NK64_MSR_FMASK, (1ULL << 9) | (1ULL << 10));
    nk64_msr_write(NK64_MSR_EFER, nk64_msr_read(NK64_MSR_EFER) | NK64_EFER_SCE);
}

void nk64_arch_init(void)
{
    nk64_uart_init();
    nk64_idt_init();
    nk64_pic_init();
    nk64_syscall_init();
    nk64_timer_init(100u);
    nk64_irq_enable();
}

void nk64_arch_idle(void)
{
    nk64_hlt();
}
