/*
 * Tiny AMD64 kernel core - polling UART ELF receiver.
 *
 * Wire format: 7 printable magic bytes "NKELFRX" plus one ignored pad byte,
 * followed by a little-endian 64-bit payload length and the raw ELF image.
 * The receiver is completely polling-based so it works from the fallback
 * shell before (or without) any interrupt setup. A watchdog bounds every
 * byte wait so a disconnected cable cannot wedge the kernel forever.
 */
#include <stdint.h>

/* kernel64.h contains a nested-comment typo we do not own. */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wcomment"
#include "nullk/kernel64.h"
#pragma GCC diagnostic pop
#include "nullk/abi.h"
#include "nullk/elf64.h"

#define NK64_UART_MAGIC         "NKELFRX"
#define NK64_UART_MAGIC_BYTES   8u
#define NK64_UART_MAGIC_CHARS   7u
#define NK64_UART_LENGTH_BYTES  8u
#define NK64_UART_MAX_PAYLOAD   0x80000u     /* 512 KiB */
#define NK64_UART_WATCHDOG      0x20000000ULL

/* Single-byte acknowledgement codes sent back to the host. */
enum {
    NK64_UART_ACK_OK = 0x00u,
    NK64_UART_ACK_BAD_MAGIC = 0x01u,
    NK64_UART_ACK_BAD_LENGTH = 0x02u,
    NK64_UART_ACK_TIMEOUT = 0x03u
};

void nk64_uart_send_ack(uint8_t code)
{
    nk64_uart_putc((char)code);
}

static int nk64_uart_read_byte(uint8_t *out)
{
    uint64_t guard = NK64_UART_WATCHDOG;

    while (guard-- != 0u) {
        int c = nk64_uart_getc();

        if (c >= 0) {
            *out = (uint8_t)c;
            return 0;
        }
    }
    return -1;
}

static int nk64_uart_expect_magic(void)
{
    uint8_t byte;
    uint32_t i;

    for (i = 0u; i < NK64_UART_MAGIC_BYTES; i++) {
        if (nk64_uart_read_byte(&byte) != 0) {
            nk64_uart_send_ack(NK64_UART_ACK_TIMEOUT);
            return -1;
        }
        if (i < NK64_UART_MAGIC_CHARS &&
            (char)byte != NK64_UART_MAGIC[i]) {
            nk64_uart_send_ack(NK64_UART_ACK_BAD_MAGIC);
            return -1;
        }
    }
    return 0;
}

static int nk64_uart_read_length(uint64_t *out)
{
    uint8_t byte;
    uint64_t len = 0u;
    uint32_t i;

    for (i = 0u; i < NK64_UART_LENGTH_BYTES; i++) {
        if (nk64_uart_read_byte(&byte) != 0) {
            nk64_uart_send_ack(NK64_UART_ACK_TIMEOUT);
            return -1;
        }
        len |= (uint64_t)byte << (i * 8u);
    }
    *out = len;
    return 0;
}

int nk64_uart_receive_elf(uint8_t *dest, uint32_t cap, uint32_t *out_len)
{
    uint64_t len = 0u;
    uint32_t i;

    if (dest == (uint8_t *)0 || out_len == (uint32_t *)0 || cap == 0u) {
        return -1;
    }
    *out_len = 0u;

    if (nk64_uart_expect_magic() != 0) {
        return -1;
    }
    if (nk64_uart_read_length(&len) != 0) {
        return -1;
    }
    if (len == 0u || len > (uint64_t)cap ||
        len > (uint64_t)NK64_UART_MAX_PAYLOAD) {
        nk64_uart_send_ack(NK64_UART_ACK_BAD_LENGTH);
        return -1;
    }

    for (i = 0u; i < (uint32_t)len; i++) {
        if (nk64_uart_read_byte(&dest[i]) != 0) {
            nk64_uart_send_ack(NK64_UART_ACK_TIMEOUT);
            return -1;
        }
    }

    *out_len = (uint32_t)len;
    nk64_uart_send_ack(NK64_UART_ACK_OK);
    return 0;
}
