ENTRY(_start)

SECTIONS
{
    . = 0x100000;

    .text : ALIGN(4096) {
        KEEP(*(.multiboot))
        KEEP(*(.text.boot))
        *(.text .text.*)
    }
    .rodata : ALIGN(4096) {
        *(.rodata .rodata.*)
    }
    .data : ALIGN(4096) {
        *(.data .data.*)
    }
    .bss : ALIGN(4096) {
        __bss_start = .;
        *boot.S.o(.bss .bss.*)
        __bss_user_start = .;
        *(.bss .bss.*)
        *(COMMON)
        __bss_end = .;
    }
    /DISCARD/ : { *(.comment) *(.note*) *(.eh_frame*) }
}
