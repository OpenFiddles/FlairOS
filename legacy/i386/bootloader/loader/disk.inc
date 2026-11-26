; disk.inc - BIOS sector reads through cylinder/head/sector translation.
;
; disk_read_chs: AX = LBA, CX = sector count, DX = segment, BX = offset.
; Returns CF = 0 on success. This is the baseline reader the boot sector uses
; because it needs no BIOS extensions; stage 2 prefers EDD (loader/edd.inc)
; and only falls back here when the firmware does not advertise it.
;
; Callers keep BX at 0 and never cross a 64 KiB boundary within one request.

disk_read_chs:
    pusha
    push es
    mov si, ax                  ; current LBA
    mov di, cx                  ; sectors still to read
    mov [chs_seg], dx
    mov [chs_off], bx
.sector:
    mov ax, si
    xor dx, dx
    mov cx, 18
    div cx                      ; AX = LBA / 18, DX = sector - 1
    inc dx
    mov [chs_sector], dl
    xor dx, dx
    mov cx, 2
    div cx                      ; AX = cylinder, DX = head
    mov [chs_cyl], ax
    mov [chs_head], dl

    mov ax, [chs_seg]
    mov es, ax
    mov bx, [chs_off]
    mov ah, 0x02
    mov al, 1
    mov ch, [chs_cyl]
    mov cl, [chs_sector]
    mov dh, [chs_head]
    mov dl, [boot_drive]
    int 0x13
    jc .fail

    inc si
    add word [chs_off], 512
    dec di
    jnz .sector
    pop es
    popa
    clc
    ret
.fail:
    pop es
    popa
    stc
    ret

boot_drive    db 0
chs_seg       dw 0
chs_off       dw 0
chs_cyl       dw 0
chs_sector    db 0
chs_head      db 0
