#ifndef NULLK_RP2040_USB_H
#define NULLK_RP2040_USB_H

#include <stdint.h>

void nkk_usb_init(void);
void nkk_usb_write(const char *text, uint32_t len);
void nkk_usb_flush(void);
void USB_IRQHandler(void);

#endif
