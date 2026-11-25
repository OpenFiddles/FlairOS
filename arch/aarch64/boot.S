/* arch/aarch64/boot.S - AArch64 entry for QEMU virt & phone bring-up */
    .section .text.boot, "ax"
    .global _start
_start:
    mrs x0, mpidr_el1
    and x0, x0, #0xff
    cbnz x0, park
    ldr x1, =_stack_top
    mov sp, x1
    ldr x2, =__bss_start
    ldr x3, =__bss_end
bss_loop:
    cmp x2, x3
    b.hs bss_done
    str xzr, [x2], #8
    b bss_loop
bss_done:
    bl kmain
park:
    wfe
    b park
