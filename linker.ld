/*
 * AMXOS Linker Script
 * The kernel is loaded at 1MB
 */
ENTRY(_start)

SECTIONS
{
    . = 1M;

    .text :
    {
        *(.multiboot)
        *(.text)
    }

    .data :
    {
        *(.data)
    }

    .bss :
    {
        *(.bss)
    }
}
