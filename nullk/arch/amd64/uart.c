/*
 * 16550A serial console for the AMD64 port.
 *
 * COM1 (0x3f8) is the only console device the kernel needs: Limine gives us a
 * long-mode environment, and QEMU wires COM1 to stdio with -nographic. The
 * driver is polled (no interrupts) so it also works before the IDT is live.
 * Port I/O and CPU helpers live here as well to keep the arch layer small.
 */

#include <stdint.h>

/* kernel64.h contains a nested-comment typo in a libc-free header we do not own. */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wcomment"
#include "nullk/kernel64.h"
#pragma GCC diagnostic pop

#define NK64_UART_BASE 0x3f8u

#define NK64_UART_RBR 0u  /* receive buffer (DLAB = 0) */
#define NK64_UART_TBR 0u  /* transmit buffer (DLAB = 0) */
#define NK64_UART_DLL 0u  /* divisor low  (DLAB = 1) */
#define NK64_UART_IER 1u
#define NK64_UART_DLH 1u  /* divisor high (DLAB = 1) */
#define NK64_UART_FCR 2u
#define NK64_UART_LCR 3u
#define NK64_UART_MCR 4u
#define NK64_UART_LSR 5u

#define NK64_UART_LSR_DR 0x01u  /* data ready */
#define NK64_UART_LSR_THRE 0x20u /* transmit holding register empty */

#define NK64_UART_DIV 3u /* 115200 baud with the QEMU UART clock */

#define NK64_KB_STATUS 0x64u
#define NK64_KB_RESET 0xfeu

void nk64_outb(uint16_t port, uint8_t value)
{
    __asm__ volatile("outb %%al, %%dx" : : "d"(port), "a"(value));
}

uint8_t nk64_inb(uint16_t port)
{
    uint8_t value;

    __asm__ volatile("inb %%dx, %%al" : "=a"(value) : "d"(port));
    return value;
}

void nk64_hlt(void)
{
    __asm__ volatile("hlt");
}

void nk64_reboot(void)
{
    for (;;) {
        /* Wait for the keyboard controller input buffer to drain, then reset. */
        while ((nk64_inb(NK64_KB_STATUS) & 0x02u) != 0u) {
        }
        nk64_outb(NK64_KB_STATUS, NK64_KB_RESET);
        for (volatile uint32_t i = 0; i < 100000u; i++) {
        }
        nk64_hlt();
    }
}

uint64_t nk64_rdtsc(void)
{
    uint32_t lo;
    uint32_t hi;

    __asm__ volatile("rdtsc" : "=a"(lo), "=d"(hi));
    return ((uint64_t)hi << 32) | (uint64_t)lo;
}

void nk64_uart_init(void)
{
    nk64_outb(NK64_UART_BASE + NK64_UART_IER, 0x00u); /* disable interrupts */
    nk64_outb(NK64_UART_BASE + NK64_UART_LCR, 0x80u); /* DLAB on */
    nk64_outb(NK64_UART_BASE + NK64_UART_DLL, (uint8_t)NK64_UART_DIV);
    nk64_outb(NK64_UART_BASE + NK64_UART_DLH, 0x00u);
    nk64_outb(NK64_UART_BASE + NK64_UART_LCR, 0x03u); /* 8N1, DLAB off */
    nk64_outb(NK64_UART_BASE + NK64_UART_FCR, 0xc7u); /* FIFO on + flush */
    nk64_outb(NK64_UART_BASE + NK64_UART_MCR, 0x0bu); /* DTR/ RTS/ OUT2 */
}

void nk64_uart_putc(char c)
{
    while ((nk64_inb(NK64_UART_BASE + NK64_UART_LSR) & NK64_UART_LSR_THRE) == 0u) {
    }
    if (c == '\n') {
        nk64_outb(NK64_UART_BASE + NK64_UART_TBR, (uint8_t)'\r');
        while ((nk64_inb(NK64_UART_BASE + NK64_UART_LSR) & NK64_UART_LSR_THRE) == 0u) {
        }
    }
    nk64_outb(NK64_UART_BASE + NK64_UART_TBR, (uint8_t)c);
}

void nk64_uart_puts(const char *text, uint32_t len)
{
    uint32_t i;

    for (i = 0; i < len; i++) {
        nk64_uart_putc(text[i]);
    }
}

int nk64_uart_getc(void)
{
    if ((nk64_inb(NK64_UART_BASE + NK64_UART_LSR) & NK64_UART_LSR_DR) == 0u) {
        return -1;
    }
    return (int)nk64_inb(NK64_UART_BASE + NK64_UART_RBR);
}
