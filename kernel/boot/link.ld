ENTRY(_start)

SECTIONS
{
    . = 0x10000;

    .text : {
        *(.text.entry)
        *(.text .text.*)
    }
    .rodata : { *(.rodata .rodata.*) }
    .data   : { *(.data .data.*) }
    .bss : {
        __bss_start = .;
        *(.bss .bss.*)
        *(COMMON)
        __bss_end = .;
    }

    /DISCARD/ : { *(.comment) *(.note*) *(.eh_frame*) }
}

ASSERT(__bss_end <= 0x30000, "kernel bss overlaps the reserved PMM bitmap and Explorer staging gap")
ASSERT(__bss_end <= 0x40000, "kernel image plus bss overlaps the app slot at 0x40000")
