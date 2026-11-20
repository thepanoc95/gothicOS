/* Boot ROM loads the 256-byte stage 2 here, above its own stack use. */
ENTRY(_start)

MEMORY
{
    SRAM (rx) : ORIGIN = 0x20041f00, LENGTH = 252
}

SECTIONS
{
    . = ORIGIN(SRAM);
    .text :
    {
        KEEP(*(.text*))
    } > SRAM
}
