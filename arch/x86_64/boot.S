/* SPDX-License-Identifier: AGPL-3.0-only */
/* arch/x86_64/boot.S - Multiboot1 32-bit -> x86_64 Long Mode entry */
#ifndef FB_INIT_W
#define FB_INIT_W 1024
#endif
#ifndef FB_INIT_H
#define FB_INIT_H 768
#endif

    .section .multiboot, "a"
    .align 4
mb_header:
    .long 0x1BADB002                          /* magic */
    .long 0x00000003                          /* flags: align | meminfo | video_mode */
    .long -(0x1BADB002 + 0x00000003)          /* checksum */
    .long 0, 0, 0, 0, 0                       /* aout kludge unused */
    .long 0                                   /* mode_type: 0 = linear graphics */
    .long FB_INIT_W                           /* width */
    .long FB_INIT_H                           /* height */
    .long 32                                  /* depth */

    .section .bss
    .align 4096
pml4:
    .skip 4096
pdpt:
    .skip 4096
pd0:
    .skip 4096
pd1:
    .skip 4096
pd2:
    .skip 4096
pd3:
    .skip 4096
    .align 16
stack_bottom:
    .skip 32768
stack_top:

    .section .rodata
    .align 8
gdt64:
    .quad 0x0000000000000000
    .quad 0x00AF9A000000FFFF                  /* 0x08: 64-bit code */
    .quad 0x00CF92000000FFFF                  /* 0x10: 64-bit data */
gdt64_ptr:
    .word (gdt64_ptr - gdt64 - 1)
    .quad gdt64

    .section .text.boot, "ax"
    .code32
    .global _start
_start:
    cli
    movl $stack_top, %esp
    /* Preserve Multiboot magic (eax) & info ptr (ebx) */
    movl %eax, %esi
    movl %ebx, %edi

    /* Clear page tables (6 * 4096 bytes) */
    pushl %edi
    movl $pml4, %edi
    xorl %eax, %eax
    movl $(6 * 1024), %ecx
    rep stosl
    popl %edi

    /* PML4[0] -> pdpt */
    movl $pdpt, %eax
    orl $0x03, %eax
    movl %eax, pml4

    /* PDPT[0..3] -> pd0..pd3 (4 GiB identity map for RAM + PCI LFB) */
    movl $pd0, %eax
    orl $0x03, %eax
    movl %eax, pdpt + 0
    movl $pd1, %eax
    orl $0x03, %eax
    movl %eax, pdpt + 8
    movl $pd2, %eax
    orl $0x03, %eax
    movl %eax, pdpt + 16
    movl $pd3, %eax
    orl $0x03, %eax
    movl %eax, pdpt + 24

    /* Fill 2048 2MiB pages (4 GiB) */
    movl $pd0, %edx
    xorl %ecx, %ecx
fill_pd:
    movl %ecx, %eax
    shll $21, %eax
    orl $0x83, %eax                           /* Present | RW | PS (2 MiB) */
    movl %eax, (%edx, %ecx, 8)
    movl %ecx, %eax
    shrl $11, %eax
    movl %eax, 4(%edx, %ecx, 8)
    incl %ecx
    cmpl $2048, %ecx
    jb fill_pd

    /* Enable PAE */
    movl %cr4, %eax
    orl $(1 << 5), %eax
    movl %eax, %cr4

    /* Load CR3 */
    movl $pml4, %eax
    movl %eax, %cr3

    /* Enable Long Mode in IA32_EFER (0xC0000080) */
    movl $0xC0000080, %ecx
    rdmsr
    orl $(1 << 8), %eax
    wrmsr

    /* Enable Paging + Protection */
    movl %cr0, %eax
    orl $0x80000001, %eax
    movl %eax, %cr0

    lgdt gdt64_ptr
    ljmp $0x08, $long_mode_start

    .code64
long_mode_start:
    movw $0x10, %ax
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %fs
    movw %ax, %gs
    movw %ax, %ss
    movq $stack_top, %rsp

    /* Enable SSE */
    movq %cr0, %rax
    andw $0xFFFB, %ax
    orw $0x0002, %ax
    movq %rax, %cr0
    movq %cr4, %rax
    orw $0x0600, %ax
    movq %rax, %cr4

    /* Save multiboot args across BSS clear */
    movl %esi, %r12d
    movl %edi, %r13d

    /* Clear BSS after page tables? Note: pml4/stack are in .bss, so only clear __bss_user_start..__bss_end */
    lea __bss_user_start(%rip), %rdi
    lea __bss_end(%rip), %rcx
    subq %rdi, %rcx
    xorl %eax, %eax
    rep stosb

    movl %r12d, %edi
    movl %r13d, %esi
    call kmain64
halt_loop:
    hlt
    jmp halt_loop
