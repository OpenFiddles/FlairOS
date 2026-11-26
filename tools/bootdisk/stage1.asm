; SPDX-License-Identifier: AGPL-3.0-only
; FlairOS 64-bit BIOS boot sector.
;
; Loads the stage-2 loader from disk with the BIOS extended disk interface and
; jumps to it. The kernel is not touched here: the loader reads it off disk
; sectors with the same interface, so this sector stays at 512 bytes.

[BITS 16]
[ORG 0x7C00]

STAGE2_LBA     equ 1
STAGE2_SECTORS equ 8
STAGE2_ADDR    equ 0x8000

start:
    cli
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7C00
    sti
    mov [drive], dl

    ; Extended disk access present? (AH=41h, BX=55AAh)
    mov ah, 0x41
    mov bx, 0x55AA
    mov dl, [drive]
    int 0x13
    jc  denied
    cmp bx, 0xAA55
    jne denied

    mov si, dap
    mov ah, 0x42
    mov dl, [drive]
    int 0x13
    jc  denied

    mov dl, [drive]
    jmp 0x0000:STAGE2_ADDR

denied:
    mov al, '!'
    call putc_vga
halt:
    hlt
    jmp halt

; Write AL to the top-left cell of the text screen.
putc_vga:
    push ax
    push bx
    push ds
    mov bx, 0xB800
    mov ds, bx
    xor bx, bx
    mov ah, 0x4F
    mov [bx], ax
    pop ds
    pop bx
    pop ax
    ret

drive: db 0
align 4
dap:
    db 0x10
    db 0
    dw STAGE2_SECTORS
    dw STAGE2_ADDR & 0xF
    dw STAGE2_ADDR >> 4
    dq STAGE2_LBA

times 510 - ($ - $$) db 0
dw 0xAA55
