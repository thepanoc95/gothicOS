/* Pico SDK architecture backend for null_kernel.
 *
 * The SDK provides clocks, the vector table, USB CDC (TinyUSB) and the C
 * runtime. Only the kernel's scheduler hooks and console are implemented here.
 */
#include <stdio.h>

#include "pico/stdlib.h"
#include "hardware/clocks.h"

#include "../nk/kern.h"
#include "../nk/arch_if.h"

#define SYSTICK_BASE            0xe000e010u
#define SYSTICK_CTRL            (*(volatile uint32_t *)(SYSTICK_BASE + 0x00u))
#define SYSTICK_LOAD            (*(volatile uint32_t *)(SYSTICK_BASE + 0x04u))
#define SYSTICK_VAL             (*(volatile uint32_t *)(SYSTICK_BASE + 0x08u))

#define ICSR                    (*(volatile uint32_t *)0xe000ed04u)
#define SHPR3                   (*(volatile uint32_t *)0xe000ed20u)
#define PENDSVSET               (1u << 28)
#define ICSR_CLR_PEND           ((1u << 27) | (1u << 25))

static inline void nkk_dsb(void) {
    __asm__ volatile("dsb" ::: "memory");
}

static inline void nkk_isb(void) {
    __asm__ volatile("isb" ::: "memory");
}

void nkk_arch_early_init(void) {
    /* SysTick (PRI_15) must stay above PendSV (PRI_14): PendSV must never
     * preempt SysTick and return to Thread mode with a nested exception. */
    SHPR3 = (0x80u << 24) | (0xffu << 16);
    ICSR = ICSR_CLR_PEND;                  /* clear stale pending switches */
    /* stdio/TinyUSB is initialized by main() before nkk_boot(). */
}

void nkk_arch_tick_init(uint32_t hz) {
    uint32_t sys_hz;

    if (hz == 0u) {
        hz = 100u;
    }
    sys_hz = clock_get_hz(clk_sys);
    SYSTICK_CTRL = 0u;
    SYSTICK_LOAD = sys_hz / hz - 1u;
    SYSTICK_VAL = 0u;
    SYSTICK_CTRL = 0x07u; /* ENABLE | TICKINT | CLKSOURCE */
}

void nkk_arch_enable_irq(void) {
    __asm__ volatile("cpsie i" ::: "memory");
}

void nkk_arch_disable_irq(void) {
    __asm__ volatile("cpsid i" ::: "memory");
}

void nkk_arch_yield(void) {
    ICSR = PENDSVSET;
    nkk_dsb();
    nkk_isb();
}

void nkk_arch_puts(const char *text, uint32_t len) {
    if (text == 0 || len == 0u) {
        return;
    }
    fwrite(text, 1u, len, stdout);
    fflush(stdout);
}

void nkk_arch_idle(void) {
    __asm__ volatile("wfi" ::: "memory");
}

void nkk_arch_start_first(void) {
    /* Stay on MSP until PendSV installs the first task stack and returns with
     * EXC_RETURN selecting PSP. Switching CONTROL here would leave PSP=0 while
     * SysTick could still fire, causing an exception-entry stack fault. */
    nkk_sched_request_switch();
    nkk_arch_enable_irq();
    for (;;) {
        nkk_arch_idle();
    }
}
