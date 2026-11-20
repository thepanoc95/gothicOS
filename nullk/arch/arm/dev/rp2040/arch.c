#include "../../../../nk/kern.h"
#include "../../../../nk/arch_if.h"
#include "usb.h"

/* Raspberry Pi Pico W: RP2040 (Cortex-M0+) at 12 MHz clk_sys/clk_peri.
 * Console is USB CDC ACM (115200 8N1 line coding) on the micro-USB port. */
#define REG32(addr)             (*(volatile uint32_t *)(addr))

#define XOSC_BASE               0x40024000u
#define XOSC_CTRL               (XOSC_BASE + 0x00u)
#define XOSC_STATUS             (XOSC_BASE + 0x04u)
#define XOSC_STARTUP            (XOSC_BASE + 0x0cu)
#define XOSC_STABLE             (1u << 31)

#define CLOCKS_BASE             0x40008000u
#define CLK_REF_CTRL            (CLOCKS_BASE + 0x30u)
#define CLK_REF_DIV             (CLOCKS_BASE + 0x34u)
#define CLK_REF_SELECTED        (CLOCKS_BASE + 0x38u)
#define CLK_SYS_CTRL            (CLOCKS_BASE + 0x3cu)
#define CLK_SYS_DIV             (CLOCKS_BASE + 0x40u)
#define CLK_SYS_SELECTED        (CLOCKS_BASE + 0x44u)
#define CLK_PERI_CTRL           (CLOCKS_BASE + 0x48u)
#define CLK_USB_CTRL            (CLOCKS_BASE + 0x54u)
#define CLK_USB_DIV             (CLOCKS_BASE + 0x58u)
#define CLK_USB_ENABLE          (1u << 11)
#define CLK_DIV_BY_1            (1u << 8) /* INT=1; INT=0 means /65536! */
#define CLK_REF_SRC_XOSC        2u
#define CLK_PERI_ENABLE         (1u << 11)

#define PLL_USB_BASE            0x4002c000u
#define PLL_CS                  (PLL_USB_BASE + 0x00u)
#define PLL_PWR                 (PLL_USB_BASE + 0x04u)
#define PLL_FBDIV               (PLL_USB_BASE + 0x08u)
#define PLL_PRIM                (PLL_USB_BASE + 0x0cu)
#define PLL_LOCK                (1u << 31)
#define PLL_PWR_PD              (1u << 0)
#define PLL_PWR_VCOPD           (1u << 5)
#define PLL_PWR_POSTDIVPD       (1u << 3)

#define RESETS_BASE             0x4000c000u
#define RESETS_RESET            (RESETS_BASE + 0x00u)
#define RESETS_RESET_DONE       (RESETS_BASE + 0x08u)
#define RESET_IO_BANK0          (1u << 5)
#define RESET_PADS_BANK0        (1u << 8)
#define RESET_PLL_USB           (1u << 13)
#define RESET_USBCTRL           (1u << 24)
#define RESET_MASK              (RESET_IO_BANK0 | RESET_PADS_BANK0 | \
                                 RESET_PLL_USB | RESET_USBCTRL)

#define SYSTICK_BASE            0xe000e010u
#define SYSTICK_CTRL            REG32(SYSTICK_BASE + 0x00u)
#define SYSTICK_LOAD            REG32(SYSTICK_BASE + 0x04u)
#define SYSTICK_VAL             REG32(SYSTICK_BASE + 0x08u)

#define ICSR                    (*(volatile uint32_t *)0xe000ed04u)
#define SHPR3                   (*(volatile uint32_t *)0xe000ed20u)
#define PENDSVSET               (1u << 28)
#define ICSR_CLR_PEND           ((1u << 27) | (1u << 25))

#define SYSTEM_HZ               12000000u

static inline void nkk_dsb(void) {
    __asm__ volatile("dsb" ::: "memory");
}

static inline void nkk_isb(void) {
    __asm__ volatile("isb" ::: "memory");
}

static void nkk_rp2040_clocks_init(void) {
    /* Leave an already-running XOSC alone; the boot ROM normally starts it. */
    if ((REG32(XOSC_STATUS) & XOSC_STABLE) == 0) {
        REG32(XOSC_CTRL) = 0xaa0u;          /* 1-15 MHz range */
        REG32(XOSC_STARTUP) = 47u;          /* ~1 ms startup delay */
        REG32(XOSC_CTRL) |= (0xfabu << 12); /* ENABLE key */
        while ((REG32(XOSC_STATUS) & XOSC_STABLE) == 0) {
            /* wait for stable 12 MHz crystal */
        }
    }

    /* The boot ROM may have left clk_usb running from PLL_USB; stop it before
     * resetting the PLL so the clock tree is not disturbed mid-cycle. */
    REG32(CLK_USB_CTRL) = 0;

    REG32(RESETS_RESET) |= (RESET_PLL_USB | RESET_USBCTRL);
    REG32(RESETS_RESET) &= ~RESET_MASK;
    while ((REG32(RESETS_RESET_DONE) & RESET_MASK) != RESET_MASK) {
        /* wait for PLL_USB and USBCTRL to leave reset */
    }

    REG32(CLK_REF_DIV) = CLK_DIV_BY_1;
    REG32(CLK_REF_CTRL) = CLK_REF_SRC_XOSC;
    while ((REG32(CLK_REF_SELECTED) & (1u << CLK_REF_SRC_XOSC)) == 0) {
        /* wait for clk_ref to switch away from any PLL source */
    }

    REG32(CLK_SYS_DIV) = CLK_DIV_BY_1;
    REG32(CLK_SYS_CTRL) = 0u;               /* clk_sys = clk_ref = 12 MHz */
    while ((REG32(CLK_SYS_SELECTED) & 1u) == 0) {
        /* wait for clk_sys to switch */
    }

    REG32(CLK_PERI_CTRL) = CLK_PERI_ENABLE; /* clk_peri = clk_sys */

    /* PLL_USB: 12 MHz / 1 * 64 = 768 MHz VCO, /4 /4 = 48 MHz for USB. */
    REG32(PLL_CS) = 1u;
    REG32(PLL_FBDIV) = 64u;
    REG32(PLL_PWR) &= ~(PLL_PWR_PD | PLL_PWR_VCOPD);
    while ((REG32(PLL_CS) & PLL_LOCK) == 0) {
        /* wait for PLL lock */
    }
    REG32(PLL_PRIM) = (4u << 16) | (4u << 12);
    REG32(PLL_PWR) &= ~PLL_PWR_POSTDIVPD;

    REG32(CLK_USB_DIV) = CLK_DIV_BY_1;
    REG32(CLK_USB_CTRL) = CLK_USB_ENABLE;   /* auxsrc 0 = PLL_USB */
}

void nkk_arch_early_init(void) {
    /* SysTick (PRI_15) must stay above PendSV (PRI_14): PendSV must never
     * preempt SysTick and return to Thread mode with a nested exception. */
    SHPR3 = (0x80u << 24) | (0xffu << 16);
    ICSR = ICSR_CLR_PEND;                  /* clear stale pending switches */

    nkk_rp2040_clocks_init();
    nkk_usb_init();
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
    nkk_usb_write(text, len);
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
    nkk_usb_flush();
    for (;;) {
        nkk_arch_idle();
    }
}
