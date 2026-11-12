.section .multiboot, "a"
.align 4
.long 0x1BADB002
.long 0
.long -(0x1BADB002 + 0)

.section .text
.global _start
_start:
    mov esp, offset stack_top
    call kmain
2:  hlt
    jmp 2b

.section .bss
.align 16
stack_bottom:
    .skip 16384
stack_top:
