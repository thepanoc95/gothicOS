/* Raspberry Pi Pico W: 2 MB XIP flash at 0x10000000, 264 KB SRAM at 0x20000000.
 * Boot stage 2 occupies the first 256 bytes of flash; our vector table starts
 * at 0x10000100, which is also where stage 2 vectors on entry. */
ENTRY(Reset_Handler)

MEMORY
{
    FLASH (rx)  : ORIGIN = 0x10000000, LENGTH = 2M
    RAM   (rwx) : ORIGIN = 0x20000000, LENGTH = 264K
}

__stack_top__ = ORIGIN(RAM) + LENGTH(RAM);

SECTIONS
{
    .isr_vector 0x10000100 :
    {
        . = ALIGN(256);
        KEEP(*(.isr_vector))
        . = ALIGN(4);
    } > FLASH

    .text :
    {
        . = ALIGN(4);
        *(.text*)
        *(.rodata*)
        . = ALIGN(4);
        __etext = .;
    } > FLASH

    .data : AT (__etext)
    {
        . = ALIGN(4);
        _sdata = .;
        *(.data*)
        . = ALIGN(4);
        _edata = .;
    } > RAM
    __sidata = LOADADDR(.data);

    .bss (NOLOAD) :
    {
        . = ALIGN(4);
        _sbss = .;
        *(.bss*)
        *(COMMON)
        . = ALIGN(4);
        _ebss = .;
    } > RAM

    /DISCARD/ :
    {
        *(.ARM.exidx*)
        *(.ARM.extab*)
        *(.eh_frame*)
        *(.comment*)
    }
}
