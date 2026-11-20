/*
 * Minimal RP2040 second-stage bootloader.
 *
 * The ROM loader copies 256 bytes from flash offset 0 to the top of SRAM and
 * branches there. This stage enables single-bit 0x03 XIP reads, then vectors
 * into the main image at flash offset 0x100. The final 4 bytes are replaced by
 * tools/rp2040_boot2.py with the boot ROM checksum.
 *
 * Register values are derived from the RP2040 datasheet (SSI, M0+ VTOR).
 * No SDK or libc dependency.
 */
.syntax unified
.cpu cortex-m0plus
.thumb

#define XIP_SSI_BASE        0x18000000
#define SSI_CTRLR0          0x00
#define SSI_CTRLR1          0x04
#define SSI_SSIENR          0x08
#define SSI_BAUDR           0x14
#define SSI_SPI_CTRLR0      0xf4

#define PPB_BASE            0xe0000000
#define M0PLUS_VTOR         0xed08
#define XIP_BASE            0x10000000
#define FLASH_IMAGE         0x10000100

/* CTRLR0: standard 1-bit frames, 32-bit data, EEPROM-read mode. */
#define CTRLR0_XIP          0x001f0300
/* SPI_CTRLR0: command 0x03, 8-bit instruction, 24-bit address, 1C1A. */
#define SPI_CTRLR0_XIP      0x03000218
#define SPI_CLKDIV          4

.section .text
.align 2
.global _start
.type _start, %function
_start:
    push {lr}

    ldr r3, =XIP_SSI_BASE
    movs r1, #0
    str r1, [r3, #SSI_SSIENR]       /* disable SSI while configuring */

    movs r1, #SPI_CLKDIV
    str r1, [r3, #SSI_BAUDR]

    ldr r1, =CTRLR0_XIP
    str r1, [r3, #SSI_CTRLR0]

    ldr r1, =SPI_CTRLR0_XIP
    ldr r0, =(XIP_SSI_BASE + SSI_SPI_CTRLR0)
    str r1, [r0]

    movs r1, #0                     /* NDF = 0: one 32-bit read per transfer */
    str r1, [r3, #SSI_CTRLR1]

    movs r1, #1
    str r1, [r3, #SSI_SSIENR]       /* XIP is live */

    pop {r0}
    cmp r0, #0
    beq .Lvector_into_flash
    bx r0                           /* called as a normal function from RAM */

.Lvector_into_flash:
    ldr r0, =FLASH_IMAGE
    ldr r1, =(PPB_BASE + M0PLUS_VTOR)
    str r0, [r1]
    ldmia r0, {r0, r1}              /* initial MSP, reset PC */
    msr msp, r0
    bx r1
.size _start, . - _start

.ltorg
.end
