/*
 * Freestanding ET_EXEC layout for NULLK ABI v3 userspace tasks.
 *
 * Base 0x400000 (NK64_USER_BASE) is 2 MiB aligned; all segments are page
 * aligned so the tiny kernel can map them with a handful of 4 KiB pages.
 * Two PT_LOAD records keep text/rodata read-only and data/bss writable.
 */
ENTRY(_start)

ASSERT(0x400000 % 0x200000 == 0, "user base must be 2 MiB aligned")

PHDRS
{
    text PT_LOAD FLAGS(5);  /* PF_R | PF_X */
    data PT_LOAD FLAGS(6);  /* PF_R | PF_W */
}

SECTIONS
{
    . = 0x400000;

    .text : {
        *(.text .text.*)
    } :text

    .rodata : ALIGN(16) {
        *(.rodata .rodata.*)
        *(.rodata.str1.1)
    } :text

    /* Start the writable segment on a fresh page (and a fresh file offset). */
    . = ALIGN(4096);
    .data : ALIGN(4096) {
        *(.data .data.*)
    } :data

    .bss : ALIGN(16) {
        *(.bss .bss.*)
        *(COMMON)
    } :data

    . = ALIGN(4096);

    /DISCARD/ : {
        *(.eh_frame)
        *(.eh_frame_hdr)
        *(.note .note.*)
        *(.comment)
        *(.GNU-stack)
    }
}
