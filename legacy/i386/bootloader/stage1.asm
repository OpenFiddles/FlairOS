; FlairOS boot sector.
;
; The boot sector does one job: bring up the console, find the loader on the
; boot drive, and jump into it. Memory map discovery, the boot menu, image
; loading, kernel verification and video setup all live in stage 2, which is
; why this file no longer knows about the disk layout beyond STAGE2_SECTORS.
;
; The entry point must be the first byte of the sector: NASM emits included
; code in place, so the helper modules are included after the boot code.
[BITS 16]
[ORG 0x7C00]

%include "bootinfo.inc"

STAGE2_LBA     equ 1
STAGE2_SECTORS equ 6

start:
    cli
    cld
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7C00
    sti
    mov [boot_drive], dl

    call serial_init

    mov si, msg_banner
    call puts16
    mov si, msg_loading
    call puts16

    xor ah, ah                  ; reset the controller before the first read
    mov dl, [boot_drive]
    int 0x13

    mov ax, STAGE2_LBA
    mov cx, STAGE2_SECTORS
    xor dx, dx
    mov bx, 0x9000
    call disk_read_chs
    jc disk_error

    mov dl, [boot_drive]
    jmp 0x0000:0x9000

disk_error:
    mov si, msg_disk
    call puts16
.hang:
    hlt
    jmp .hang

msg_banner  db "FlairOS boot", 13, 10, 0
msg_loading db "boot: loading the FlairOS loader", 13, 10, 0
msg_disk    db "boot: cannot read the loader from the boot drive", 13, 10, 0

%include "loader/serial.inc"
%include "loader/disk.inc"

times 510 - ($ - $$) db 0
dw 0xAA55
