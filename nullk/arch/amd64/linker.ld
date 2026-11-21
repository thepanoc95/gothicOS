/*
 * Limine-loaded higher-half AMD64 kernel.
 *
 * The image lives in the top 2 GiB so that sign-extended kernel addresses work
 * and Limine's requirement on the kernel virtual address is met. The request
 * table owns its own PT_LOAD so the bootloader can find it, and the start/end
 * markers bracket it in the final ELF.
 */

OUTPUT_FORMAT(elf64-x86-64)
ENTRY(nk64_entry)

PHDRS
{
    limine_requests PT_LOAD;
    text PT_LOAD;
    rodata PT_LOAD;
    data PT_LOAD;
}

SECTIONS
{
    . = 0xffffffff80000000;
    _kernel_start = .;

    .limine_requests :
    {
        KEEP(*(.limine_requests_start))
        KEEP(*(.limine_requests))
        KEEP(*(.limine_requests_end))
    } :limine_requests

    . = ALIGN(0x1000);
    _text_start = .;
    .text :
    {
        *(.text .text.*)
    } :text

    . = ALIGN(0x1000);
    _text_end = .;
    .rodata :
    {
        *(.rodata .rodata.*)
    } :rodata

    . = ALIGN(0x1000);
    _data_start = .;
    .data :
    {
        *(.data .data.*)
    } :data

    . = ALIGN(0x1000);
    _data_end = .;
    _bss_start = .;
    .bss (NOLOAD) :
    {
        *(.bss .bss.*)
        *(COMMON)
    } :data

    . = ALIGN(0x1000);
    _bss_end = .;
    _kernel_end = .;

    /DISCARD/ :
    {
        *(.eh_frame*)
        *(.note .note.*)
        *(.comment)
    }
}
