/* Minimal baremetal USB CDC ACM console for RP2040.
 *
 * Implements just enough USB device support for a terminal: EP0 control
 * transfers (standard + CDC class requests), EP2 IN bulk output and EP2 OUT
 * bulk input discard. No SDK, no libc, no dynamic allocation. All state is
 * static and all buffers live in USB DPRAM or the kernel image.
 */
#include "usb.h"

#define REG32(addr)             (*(volatile uint32_t *)(addr))
#define REG8(addr)              (*(volatile uint8_t *)(addr))

#define USBCTRL_REGS            0x50100000u
#define USBCTRL_DPRAM           0x50100100u
#define USB_DPRAM_WORDS         1024u /* 4 KiB */

#define USB_ADDR_ENDP           REG32(USBCTRL_REGS + 0x000u)
#define USB_MAIN_CTRL           REG32(USBCTRL_REGS + 0x040u)
#define USB_SIE_CTRL            REG32(USBCTRL_REGS + 0x04cu)
#define USB_SIE_STATUS          REG32(USBCTRL_REGS + 0x050u)
#define USB_BUFF_STATUS         REG32(USBCTRL_REGS + 0x058u)
#define USB_EP_STALL_ARM        REG32(USBCTRL_REGS + 0x068u)
#define USB_USB_MUXING          REG32(USBCTRL_REGS + 0x074u)
#define USB_USB_PWR             REG32(USBCTRL_REGS + 0x078u)
#define USB_INTE                REG32(USBCTRL_REGS + 0x090u)
#define USB_INTS                REG32(USBCTRL_REGS + 0x098u)

#define MAIN_CTRL_EN            0x00000001u /* device mode, controller on */
#define SIE_CTRL_EP0_INT_1BUF   0x20000000u
#define SIE_CTRL_PULLUP_EN      0x00010000u
#define SIE_STATUS_RESUME       0x00000800u
#define SIE_STATUS_SUSPENDED    0x00000010u
#define SIE_STATUS_SETUP_REC    0x00020000u
#define SIE_STATUS_BUS_RESET    0x00080000u

#define INTS_BUFF_STATUS        (1u << 4)
#define INTS_BUS_RESET          (1u << 12)
#define INTS_DEV_SUSPEND        (1u << 14)
#define INTS_DEV_RESUME         (1u << 15)
#define INTS_SETUP_REQ          (1u << 16)

#define MUXING_TO_PHY           0x00000001u
#define MUXING_SOFTCON          0x00000008u
#define PWR_VBUS_DETECT         0x00000004u
#define PWR_VBUS_OVERRIDE       0x00000008u

/* DPRAM layout (see RP2040 datasheet, USB section). */
#define DPRAM_SETUP             0x000u
#define DPRAM_EP_CTRL(n, out)   (0x008u + ((n) - 1u) * 8u + ((out) ? 4u : 0u))
#define DPRAM_EP_BUF_CTRL(n, out) (0x080u + (n) * 8u + ((out) ? 4u : 0u))
#define DPRAM_EP0_BUF           0x100u
#define DPRAM_EP2_IN_BUF        0x180u
#define DPRAM_EP2_OUT_BUF       0x1c0u

#define BUF_CTRL_FULL           0x00008000u
#define BUF_CTRL_LAST           0x00004000u
#define BUF_CTRL_DATA1          0x00002000u
#define BUF_CTRL_SEL            0x00001000u
#define BUF_CTRL_STALL          0x00000800u
#define BUF_CTRL_AVAIL          0x00000400u
#define BUF_CTRL_LEN_MASK       0x000003ffu

#define EP_CTRL_ENABLE          0x80000000u
#define EP_CTRL_INT_PER_BUFFER  0x20000000u
#define EP_CTRL_BULK            (2u << 26)
#define EP_CTRL_BUF_TYPE_SHIFT  26u

#define NVIC_ISER0              REG32(0xe000e100u)
#define NVIC_IPR(n)             REG8(0xe000e400u + (n))
#define USBCTRL_IRQ             5
#define USBCTRL_IRQ_PRIO        0x80u

#define TX_RING_SIZE            4096u
#define TX_RING_MASK            (TX_RING_SIZE - 1u)
#define EP0_PACKET_SIZE         64u
#define EP2_PACKET_SIZE         64u

static uint8_t nkk_usb_tx_ring[TX_RING_SIZE];
static volatile uint32_t nkk_usb_tx_head;
static volatile uint32_t nkk_usb_tx_tail;

static volatile uint8_t nkk_usb_configured;
static volatile uint8_t nkk_usb_addr_pending;
static volatile uint8_t nkk_usb_ep2_busy;
static volatile uint8_t nkk_usb_ep2_pid;
static volatile uint8_t nkk_usb_ep2_out_pid;
static volatile uint8_t nkk_usb_ep0_out_data;

static const uint8_t *nkk_usb_ep0_data;
static volatile uint32_t nkk_usb_ep0_remaining;
static volatile uint8_t nkk_usb_ep0_pid;

static const uint8_t nkk_usb_dev_desc[] = {
    18, 1, 0x00, 0x02,       /* bLength, type, bcdUSB 2.0 */
    0x02, 0x02, 0x01,        /* CDC ACM: class 2, subclass 2, protocol 1 */
    64,                      /* bMaxPacketSize0 */
    0x8a, 0x2e,              /* VID 0x2e8a (Raspberry Pi) */
    0x0a, 0x00,              /* PID 0x000a (Pico CDC) */
    0x00, 0x01,              /* bcdDevice 1.0 */
    1, 2, 3,                 /* manufacturer, product, serial */
    1                        /* bNumConfigurations */
};

/* 9 + 8 + 9 + 5 + 5 + 4 + 5 + 7 + 9 + 7 + 7 = 75 bytes */
static const uint8_t nkk_usb_cfg_desc[] = {
    9, 2, 75, 0, 2, 1, 0, 0x80, 50,
    8, 11, 0, 2, 2, 2, 1, 0,
    9, 4, 0, 0, 1, 2, 2, 1, 0,
    5, 0x24, 0, 0x10, 0x01,
    5, 0x24, 1, 0x00, 1,
    4, 0x24, 2, 0x02,
    5, 0x24, 6, 0, 1,
    7, 5, 0x81, 3, 8, 0, 0xff,
    9, 4, 1, 0, 2, 0x0a, 0, 0, 0,
    7, 5, 0x82, 2, 64, 0, 0,
    7, 5, 0x02, 2, 64, 0, 0
};

static const uint8_t nkk_usb_str0[] = {4, 3, 0x09, 0x04};
static const uint8_t nkk_usb_str1[] = {12, 3, 'n', 0, 'u', 0, 'l', 0, 'l', 0, 'k', 0};
static const uint8_t nkk_usb_str2[] = {
    20, 3, 'n', 0, 'u', 0, 'l', 0, 'l', 0, 'k', 0,
    ' ', 0, 'C', 0, 'D', 0, 'C', 0
};
static const uint8_t nkk_usb_str3[] = {10, 3, '0', 0, '0', 0, '0', 0, '1', 0};

/* 115200 8N1 line coding, little-endian baud rate. */
static const uint8_t nkk_usb_line_coding[] = {
    0x00, 0xc2, 0x01, 0x00, 0, 0, 8
};

static inline uint32_t nkk_usb_irq_save(void) {
    uint32_t primask;
    __asm__ volatile("mrs %0, primask\n\tcpsid i" : "=r"(primask) : : "memory");
    return primask;
}

static inline void nkk_usb_irq_restore(uint32_t primask) {
    __asm__ volatile("msr primask, %0" : : "r"(primask) : "memory");
}

static inline volatile uint32_t *nkk_usb_ep_ctrl(uint32_t ep, uint32_t out) {
    return &REG32(USBCTRL_DPRAM + DPRAM_EP_CTRL(ep, out));
}

static inline volatile uint32_t *nkk_usb_ep_buf_ctrl(uint32_t ep, uint32_t out) {
    return &REG32(USBCTRL_DPRAM + DPRAM_EP_BUF_CTRL(ep, out));
}

static inline volatile uint8_t *nkk_usb_dpram(uint32_t offset) {
    return &REG8(USBCTRL_DPRAM + offset);
}

static void nkk_usb_configure_endpoints(void) {
    *nkk_usb_ep_ctrl(2, 0) = EP_CTRL_ENABLE | EP_CTRL_INT_PER_BUFFER |
                             EP_CTRL_BULK | DPRAM_EP2_IN_BUF;
    *nkk_usb_ep_ctrl(2, 1) = EP_CTRL_ENABLE | EP_CTRL_INT_PER_BUFFER |
                             EP_CTRL_BULK | DPRAM_EP2_OUT_BUF;
    *nkk_usb_ep_buf_ctrl(2, 0) = 0;
    *nkk_usb_ep_buf_ctrl(2, 1) = BUF_CTRL_AVAIL | BUF_CTRL_SEL; /* DATA0 */
}

static void nkk_usb_bus_reset(void) {
    /* Keep this minimal, matching the known-good low-level example. Do not
     * sweep DPRAM here: a SETUP packet may already be waiting at offset 0. */
    *nkk_usb_ep_buf_ctrl(0, 0) = 0;
    *nkk_usb_ep_buf_ctrl(0, 1) = 0;
    *nkk_usb_ep_buf_ctrl(2, 0) = 0;
    *nkk_usb_ep_buf_ctrl(2, 1) = 0;
    USB_EP_STALL_ARM = 0;
    USB_ADDR_ENDP = 0;

    nkk_usb_configured = 0;
    nkk_usb_addr_pending = 0;
    nkk_usb_ep2_busy = 0;
    nkk_usb_ep2_pid = 0;
    nkk_usb_ep2_out_pid = 0;
    nkk_usb_ep0_out_data = 0;
    nkk_usb_ep0_remaining = 0;
    nkk_usb_ep0_data = 0;

    nkk_usb_configure_endpoints();
}

void nkk_usb_init(void) {
    volatile uint32_t *p;
    uint32_t i;

    for (i = 0; i < USB_DPRAM_WORDS; i++) {
        p = &REG32(USBCTRL_DPRAM + i * 4);
        *p = 0;
    }

    nkk_usb_tx_head = 0;
    nkk_usb_tx_tail = 0;
    nkk_usb_configured = 0;
    nkk_usb_addr_pending = 0;
    nkk_usb_ep2_busy = 0;
    nkk_usb_ep2_pid = 0;
    nkk_usb_ep2_out_pid = 0;
    nkk_usb_ep0_out_data = 0;
    nkk_usb_ep0_remaining = 0;
    nkk_usb_ep0_data = 0;

    nkk_usb_configure_endpoints();

    USB_USB_MUXING = MUXING_TO_PHY | MUXING_SOFTCON;
    USB_USB_PWR = PWR_VBUS_DETECT | PWR_VBUS_OVERRIDE;
    USB_MAIN_CTRL = MAIN_CTRL_EN;
    USB_ADDR_ENDP = 0;
    USB_SIE_CTRL = SIE_CTRL_EP0_INT_1BUF;
    USB_INTE = 0;
    USB_SIE_STATUS = SIE_STATUS_SETUP_REC | SIE_STATUS_BUS_RESET |
                     SIE_STATUS_SUSPENDED | SIE_STATUS_RESUME;

    NVIC_IPR(USBCTRL_IRQ) = USBCTRL_IRQ_PRIO;
    NVIC_ISER0 = 1u << USBCTRL_IRQ;

    USB_INTE = INTS_BUFF_STATUS | INTS_BUS_RESET | INTS_DEV_SUSPEND |
               INTS_DEV_RESUME | INTS_SETUP_REQ;
    USB_SIE_CTRL |= SIE_CTRL_PULLUP_EN;
}

static void nkk_usb_ep0_send_chunk(void) {
    volatile uint8_t *dst = nkk_usb_dpram(DPRAM_EP0_BUF);
    uint32_t remaining = nkk_usb_ep0_remaining;
    uint32_t count = remaining < EP0_PACKET_SIZE ? remaining : EP0_PACKET_SIZE;
    uint32_t ctrl = count | BUF_CTRL_FULL | BUF_CTRL_AVAIL | BUF_CTRL_SEL;
    uint32_t i;

    for (i = 0; i < count; i++) {
        dst[i] = nkk_usb_ep0_data[i];
    }
    if (nkk_usb_ep0_pid) {
        ctrl |= BUF_CTRL_DATA1;
    }
    if (remaining == count) {
        ctrl |= BUF_CTRL_LAST;
    }
    *nkk_usb_ep_buf_ctrl(0, 0) = ctrl;

    nkk_usb_ep0_data += count;
    nkk_usb_ep0_remaining = remaining - count;
    nkk_usb_ep0_pid ^= 1u;
}

static void nkk_usb_ep0_send(const uint8_t *data, uint32_t len) {
    nkk_usb_ep0_data = data;
    nkk_usb_ep0_remaining = len;
    nkk_usb_ep0_pid = 1u; /* control data stage always starts on DATA1 */
    /* Keep EP0 OUT ready for the status ZLP the host sends after an IN data
     * stage. Without this the SIE NAKs the status packet forever and the
     * host never completes enumeration. DATA1 is the correct status PID. */
    *nkk_usb_ep_buf_ctrl(0, 1) = BUF_CTRL_AVAIL | BUF_CTRL_DATA1 | BUF_CTRL_SEL;
    nkk_usb_ep0_send_chunk();
}

static void nkk_usb_ep0_send_zlp(void) {
    static const uint8_t dummy = 0;
    nkk_usb_ep0_send(&dummy, 0);
}

static void nkk_usb_ep0_stall(void) {
    USB_EP_STALL_ARM = 1u; /* arm EP0 IN stall */
    *nkk_usb_ep_buf_ctrl(0, 0) = BUF_CTRL_STALL | BUF_CTRL_SEL;
    *nkk_usb_ep_buf_ctrl(0, 1) = BUF_CTRL_STALL | BUF_CTRL_SEL;
    nkk_usb_ep0_remaining = 0;
    nkk_usb_ep0_out_data = 0;
}

static void nkk_usb_handle_descriptor(uint16_t wValue, uint16_t wLength) {
    const uint8_t *desc = 0;
    uint32_t len = 0;
    uint32_t type = wValue >> 8;
    uint32_t index = wValue & 0xffu;

    switch (type) {
    case 1:
        desc = nkk_usb_dev_desc;
        len = sizeof(nkk_usb_dev_desc);
        break;
    case 2:
        desc = nkk_usb_cfg_desc;
        len = sizeof(nkk_usb_cfg_desc);
        break;
    case 3:
        switch (index) {
        case 0: desc = nkk_usb_str0; len = sizeof(nkk_usb_str0); break;
        case 1: desc = nkk_usb_str1; len = sizeof(nkk_usb_str1); break;
        case 2: desc = nkk_usb_str2; len = sizeof(nkk_usb_str2); break;
        case 3: desc = nkk_usb_str3; len = sizeof(nkk_usb_str3); break;
        default: break;
        }
        break;
    default:
        break;
    }

    if (desc == 0) {
        nkk_usb_ep0_stall();
        return;
    }
    if (len > wLength) {
        len = wLength;
    }
    nkk_usb_ep0_send(desc, len);
}

static void nkk_usb_handle_setup(void) {
    volatile uint8_t *src = nkk_usb_dpram(DPRAM_SETUP);
    uint8_t req[8];
    uint32_t i;
    uint16_t wValue;
    uint16_t wLength;
    uint32_t recipient;

    for (i = 0; i < 8; i++) {
        req[i] = src[i];
    }
    USB_EP_STALL_ARM = 0; /* a previous STALL must not poison this request */

    /* Abort any leftover EP0 transfer and reset the data PID for the new
     * control transfer (the host always starts the data stage on DATA1). */
    *nkk_usb_ep_buf_ctrl(0, 0) = 0;
    *nkk_usb_ep_buf_ctrl(0, 1) = 0;
    nkk_usb_ep0_pid = 1u;
    nkk_usb_ep0_remaining = 0;

    wValue = (uint16_t)(req[2] | ((uint16_t)req[3] << 8));
    wLength = (uint16_t)(req[6] | ((uint16_t)req[7] << 8));
    recipient = req[0] & 0x1fu;
    nkk_usb_ep0_out_data = 0;

    if ((req[0] & 0x60u) == 0x00u) { /* standard request */
        switch (req[1]) {
        case 0x00: /* GET_STATUS: 2 bytes of zero status */
            {
                static const uint8_t status[2] = {0, 0};
                uint32_t len = sizeof(status);
                if (len > wLength) {
                    len = wLength;
                }
                nkk_usb_ep0_send(status, len);
            }
            break;
        case 0x01: /* CLEAR_FEATURE */
        case 0x03: /* SET_FEATURE */
            nkk_usb_ep0_send_zlp();
            break;
        case 0x05: /* SET_ADDRESS */
            nkk_usb_addr_pending = (uint8_t)(wValue & 0x7fu);
            nkk_usb_ep0_send_zlp();
            break;
        case 0x06: /* GET_DESCRIPTOR */
            nkk_usb_handle_descriptor(wValue, wLength);
            break;
        case 0x08: /* GET_CONFIGURATION */
            {
                static uint8_t config_value;
                config_value = nkk_usb_configured ? 1u : 0u;
                nkk_usb_ep0_send(&config_value, 1);
            }
            break;
        case 0x09: /* SET_CONFIGURATION */
            nkk_usb_configured = (wValue != 0u);
            nkk_usb_ep0_send_zlp();
            nkk_usb_flush();
            break;
        case 0x0a: /* GET_INTERFACE */
            {
                static uint8_t alt_setting;
                alt_setting = 0;
                nkk_usb_ep0_send(&alt_setting, 1);
            }
            break;
        default:
            nkk_usb_ep0_stall();
            break;
        }
        return;
    }

    if ((req[0] & 0x60u) == 0x20u && recipient == 1u) { /* CDC interface */
        switch (req[1]) {
        case 0x20: /* SET_LINE_CODING */
            if (wLength >= 7u) {
                *nkk_usb_ep_buf_ctrl(0, 1) =
                    (BUF_CTRL_AVAIL | BUF_CTRL_DATA1 | BUF_CTRL_SEL | 7u);
                nkk_usb_ep0_out_data = 1;
            } else {
                nkk_usb_ep0_stall();
            }
            break;
        case 0x21: /* GET_LINE_CODING */
            {
                uint32_t len = sizeof(nkk_usb_line_coding);
                if (len > wLength) {
                    len = wLength;
                }
                nkk_usb_ep0_send(nkk_usb_line_coding, len);
            }
            break;
        case 0x22: /* SET_CONTROL_LINE_STATE */
        case 0x23: /* SEND_BREAK */
            nkk_usb_ep0_send_zlp();
            break;
        default:
            nkk_usb_ep0_stall();
            break;
        }
        return;
    }

    nkk_usb_ep0_stall();
}

void nkk_usb_flush(void) {
    volatile uint8_t *dst = nkk_usb_dpram(DPRAM_EP2_IN_BUF);
    uint32_t primask = nkk_usb_irq_save();
    uint32_t available = nkk_usb_tx_head - nkk_usb_tx_tail;
    uint32_t count;
    uint32_t ctrl;
    uint32_t i;

    if (!nkk_usb_configured || nkk_usb_ep2_busy || available == 0) {
        nkk_usb_irq_restore(primask);
        return;
    }

    count = available < EP2_PACKET_SIZE ? available : EP2_PACKET_SIZE;
    for (i = 0; i < count; i++) {
        dst[i] = nkk_usb_tx_ring[(nkk_usb_tx_tail + i) & TX_RING_MASK];
    }
    nkk_usb_tx_tail += count;

    ctrl = count | BUF_CTRL_FULL | BUF_CTRL_AVAIL | BUF_CTRL_LAST | BUF_CTRL_SEL;
    if (nkk_usb_ep2_pid) {
        ctrl |= BUF_CTRL_DATA1;
    }
    nkk_usb_ep2_pid ^= 1u;
    nkk_usb_ep2_busy = 1;
    *nkk_usb_ep_buf_ctrl(2, 0) = ctrl;

    nkk_usb_irq_restore(primask);
}

void nkk_usb_write(const char *text, uint32_t len) {
    uint32_t primask = nkk_usb_irq_save();
    uint32_t i;

    for (i = 0; i < len; i++) {
        if (nkk_usb_tx_head - nkk_usb_tx_tail >= TX_RING_SIZE) {
            break; /* drop new data rather than overwrite unread output */
        }
        nkk_usb_tx_ring[nkk_usb_tx_head & TX_RING_MASK] = (uint8_t)text[i];
        nkk_usb_tx_head++;
    }
    nkk_usb_irq_restore(primask);

    nkk_usb_flush();
}

static void nkk_usb_handle_buff_status(uint32_t status) {
    if (status & 0x00000001u) { /* EP0 IN */
        if (nkk_usb_ep0_remaining != 0) {
            nkk_usb_ep0_send_chunk();
        } else {
            *nkk_usb_ep_buf_ctrl(0, 0) = 0;
            if (nkk_usb_addr_pending) {
                USB_ADDR_ENDP = nkk_usb_addr_pending;
                nkk_usb_addr_pending = 0;
            }
        }
    }
    if (status & 0x00000002u) { /* EP0 OUT */
        if (nkk_usb_ep0_out_data) {
            nkk_usb_ep0_out_data = 0;
            nkk_usb_ep0_send_zlp(); /* status stage for class OUT request */
        }
        *nkk_usb_ep_buf_ctrl(0, 1) = 0;
    }
    if (status & 0x00000010u) { /* EP2 IN */
        *nkk_usb_ep_buf_ctrl(2, 0) = 0;
        nkk_usb_ep2_busy = 0;
        nkk_usb_flush();
    }
    if (status & 0x00000020u) { /* EP2 OUT: discard host input */
        uint32_t ctrl = BUF_CTRL_AVAIL | BUF_CTRL_SEL;
        if (nkk_usb_ep2_out_pid) {
            ctrl |= BUF_CTRL_DATA1;
        }
        nkk_usb_ep2_out_pid ^= 1u;
        *nkk_usb_ep_buf_ctrl(2, 1) = ctrl;
    }
}

void USB_IRQHandler(void) {
    uint32_t status = USB_INTS;

    if (status & INTS_BUFF_STATUS) {
        uint32_t buff_status = USB_BUFF_STATUS;
        USB_BUFF_STATUS = buff_status;
        nkk_usb_handle_buff_status(buff_status);
    }
    if (status & INTS_BUS_RESET) {
        nkk_usb_bus_reset();
        USB_SIE_STATUS = SIE_STATUS_BUS_RESET;
    }
    if (status & INTS_SETUP_REQ) {
        nkk_usb_handle_setup();
        USB_SIE_STATUS = SIE_STATUS_SETUP_REC;
    }
    if (status & (INTS_DEV_SUSPEND | INTS_DEV_RESUME)) {
        USB_SIE_STATUS = SIE_STATUS_SUSPENDED | SIE_STATUS_RESUME;
    }
}
