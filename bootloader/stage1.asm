; Stage 1 loads Explorer and Terminal from reserved sectors after filesystem
; data (LBA 2447..2542), stages them below TX_APP_BASE, and probes optional
; MicroPython at LBA 2623..2879. The primary filesystem occupies LBA 398..2446.
[BITS 16]
[ORG 0x7C00]

STAGE2_SECTORS equ 4
KERNEL_SECTORS equ 256
APP_SECTORS    equ 128
EXPLORER_LBA     equ 2447
EXPLORER_SECTORS equ 32
TERMINAL_LBA     equ 2479
TERMINAL_SECTORS equ 64
PYTHON_SECTORS   equ 257

start:
    cli
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7C00
    sti
    mov [boot_drive], dl

    mov si, msg_loading
    call print16

    ; Load Stage 2 to 0x0000:0x9000
    xor ax, ax
    mov es, ax
    mov word [lba], 1
    mov word [offset], 0x9000
    mov word [remaining], STAGE2_SECTORS
    call load_range

    ; Load Kernel to 0x1000:0x0000 (Physical 0x10000)
    mov ax, 0x1000
    mov es, ax
    mov word [offset], 0
    mov word [remaining], KERNEL_SECTORS
    call load_range

    ; Load App to 0x4000:0x0000 (Physical 0x40000)
    mov ax, 0x4000
    mov es, ax
    mov word [offset], 0
    mov word [remaining], APP_SECTORS
    call load_range

    ; Load explorer.airp to 0x3400:0x0000 (Physical 0x34000).
    ; Its 16 KiB staging buffer ends at 0x38000.
    mov ax, 0x3400
    mov es, ax
    mov word [lba], EXPLORER_LBA
    mov word [offset], 0
    mov word [remaining], EXPLORER_SECTORS
    call load_range

    ; Load terminal.airp to 0x3800:0x0000 (Physical 0x38000).
    ; Its 32 KiB staging buffer ends at TX_APP_BASE (0x40000).
    mov ax, 0x3800
    mov es, ax
    mov word [lba], TERMINAL_LBA
    mov word [offset], 0
    mov word [remaining], TERMINAL_SECTORS
    call load_range

    ; Probe the optional MicroPython AIRP slot. Only load the remaining sectors
    ; when its first sector contains the AIRP magic.
    mov ax, 0x6000
    mov es, ax
    mov word [lba], 2623
    mov word [offset], 0
    mov word [remaining], 1
    call load_range
    cmp dword [es:0], 0x50524941
    jne .no_python
    mov word [lba], 2624
    mov word [offset], 512
    mov word [remaining], PYTHON_SECTORS - 1
    call load_range
.no_python:

    ; Pass boot drive to Stage 2 in DL and jump
    mov dl, [boot_drive]
    jmp 0x0000:0x9000

load_range:
.load:
    cmp word [remaining], 0
    je .done
    mov ax, [lba]
    xor dx, dx
    mov cx, 36
    div cx
    mov [track], al
    mov ax, dx
    xor dx, dx
    mov cx, 18
    div cx
    mov dh, al
    mov cl, dl
    inc cl
    mov ch, [track]
    mov dl, [boot_drive]
    mov bx, [offset]
    mov ax, 0x0201
    int 0x13
    jc disk_error
    
    ; Handle segment overflow checks safely
    add word [offset], 512
    jnz .no_carry
    mov ax, es
    add ax, 0x1000
    mov es, ax
.no_carry:
    inc word [lba]
    dec word [remaining]
    jmp .load
.done:
    ret

disk_error:
    mov si, msg_disk
    call print16
.hang:
    hlt
    jmp .hang

print16:
    lodsb
    test al, al
    jz .done
    mov ah, 0x0E
    mov bh, 0x00
    int 0x10
    jmp print16
.done:
    ret

boot_drive  db 0
lba         dw 0
offset      dw 0
remaining   dw 0
track       db 0
msg_loading db "Loading FlairOS...", 13, 10, 0
msg_disk    db "Disk read error", 13, 10, 0

times 510 - ($ - $$) db 0
dw 0xAA55
