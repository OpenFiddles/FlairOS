[BITS 32]
section .text.entry
global _start
extern kmain
extern __bss_start
extern __bss_end

; The loader (legacy/i386/bootloader/loader/images.inc) checks this header before it jumps
; here. The entry point is the two-byte jump; the header it skips declares what
; the image is and how many sectors it should occupy.
;
;   +0  EB xx        jump over the header
;   +2  "FLRK"       image magic
;   +6  header version
;   +8  image size in 512-byte sectors (keep in step with KERNEL_SECTORS)
_start:
    jmp short .init
    dd 0x4B524C46
    dw 1
    dw 256

.init:
    mov edi, __bss_start
    mov ecx, __bss_end
    sub ecx, edi
    xor eax, eax
    cld
    rep stosb
    call kmain
.halt:
    cli
    hlt
    jmp .halt
