/* arch/aarch64/boot.S - AArch64 entry for the QEMU virt board */
    .section .text.boot, "ax"
    .global _start
_start:
    mrs x0, mpidr_el1
    and x0, x0, #0xff
    cbnz x0, park
    ldr x1, =_stack_top
    mov sp, x1
    bl kmain
park:
    wfe
    b park
