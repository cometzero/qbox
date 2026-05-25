ENTRY(Reset_Handler)

MEMORY
{
    rom (rx) : ORIGIN = 0x00000000, LENGTH = 128K
}

_stack_top = ORIGIN(rom) + LENGTH(rom);

SECTIONS
{
    .isr_vector ORIGIN(rom) :
    {
        KEEP(*(.isr_vector))
    } > rom

    .text :
    {
        *(.text*)
        *(.rodata*)
        . = ALIGN(4);
    } > rom
}
