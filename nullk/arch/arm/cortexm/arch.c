#include "../../../nk/kern.h"
#include "../../../nk/arch_if.h"

/* QEMU lm3s6965evb: Cortex-M3, UART0 at 0x4000c000, 12 MHz system clock. */
#define UART_BASE       0x4000c000u
#define UART_DR         (UART_BASE + 0x00u)
#define UART_FR         (UART_BASE + 0x18u)
#define UART_IBRD       (UART_BASE + 0x24u)
#define UART_FBRD       (UART_BASE + 0x28u)
#define UART_LCR_H      (UART_BASE + 0x2cu)
#define UART_CR         (UART_BASE + 0x30u)
#define UART_ICR        (UART_BASE + 0x44u)
#define UART_FR_TXFF    (1u << 5)

#define SYSTICK_BASE    0xe000e010u
#define SYSTICK_CTRL    (*(volatile uint32_t *)(SYSTICK_BASE + 0x00u))
#define SYSTICK_LOAD    (*(volatile uint32_t *)(SYSTICK_BASE + 0x04u))
#define SYSTICK_VAL     (*(volatile uint32_t *)(SYSTICK_BASE + 0x08u))

#define ICSR            (*(volatile uint32_t *)0xe000ed04u)
#define SHPR3           (*(volatile uint32_t *)0xe000ed20u)
#define PENDSVSET       (1u << 28)
#define SYSTEM_HZ       12000000u

#define REG32(addr)     (*(volatile uint32_t *)(addr))

static inline void nkk_dsb(void) {
    __asm__ volatile("dsb" ::: "memory");
}

static inline void nkk_isb(void) {
    __asm__ volatile("isb" ::: "memory");
}

void nkk_arch_early_init(void) {
    /* SHPR3: SysTick (PRI_15) above PendSV (PRI_14). PendSV must never
     * preempt SysTick, otherwise it would return to Thread mode with a
     * nested exception still active and Cortex-M3 would raise INVPC. */
    SHPR3 = (0x80u << 24) | (0xffu << 16);

    /* Minimal UART setup; QEMU does not require an exact baud divisor. */
    REG32(UART_CR) = 0;
    REG32(UART_ICR) = 0x7fu;
    REG32(UART_IBRD) = 6;
    REG32(UART_FBRD) = 0;
    REG32(UART_LCR_H) = 0x70u; /* 8N1, FIFO enabled */
    REG32(UART_CR) = 0x301u;   /* UARTEN, TXE, RXE */
}

void nkk_arch_tick_init(uint32_t hz) {
    if (hz == 0) {
        hz = 100;
    }
    SYSTICK_CTRL = 0;
    SYSTICK_LOAD = (SYSTEM_HZ / hz) - 1u;
    SYSTICK_VAL = 0;
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
    for (uint32_t i = 0; i < len; i++) {
        while ((REG32(UART_FR) & UART_FR_TXFF) != 0) {
            /* wait for TX FIFO space */
        }
        REG32(UART_DR) = (uint32_t)text[i];
    }
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
