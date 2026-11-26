; edd.inc - reads through BIOS extended disk access (INT 13h AH=42h).
;
; Requires loader/disk.inc for boot_drive and the CHS fallback.
; disk_read: AX = LBA, CX = sector count (1..64), DX = segment, BX = offset.
; Returns CF = 0 on success, and retries a failing transfer three times after
; resetting the controller.

EDD_PACKET_SIZE equ 16

; Record whether the boot drive supports extended disk access.
disk_probe:
    mov byte [edd_ok], 0
    push ax
    push bx
    push cx
    push dx
    mov ah, 0x41
    mov bx, 0x55AA
    mov dl, [boot_drive]
    int 0x13
    jc .done
    cmp bx, 0xAA55
    jne .done
    test cl, 1                  ; bit 0: extended disk access supported
    jz .done
    mov byte [edd_ok], 1
.done:
    pop dx
    pop cx
    pop bx
    pop ax
    ret

disk_read:
    cmp byte [edd_ok], 0
    je disk_read_chs

disk_read_edd:
    pusha
    push es
    mov [dap_count], cx
    mov [dap_off], bx
    mov [dap_seg], dx
    mov word [dap_lba], ax
    mov word [dap_lba + 2], 0
    mov word [dap_lba + 4], 0
    mov word [dap_lba + 6], 0
    mov byte [disk_retries], 3
.retry:
    mov si, dap
    mov dl, [boot_drive]
    mov ah, 0x42
    int 0x13
    jnc .ok
    xor ah, ah                  ; reset the controller and try again
    mov dl, [boot_drive]
    int 0x13
    dec byte [disk_retries]
    jnz .retry
    pop es
    popa
    stc
    ret
.ok:
    pop es
    popa
    clc
    ret

edd_ok        db 0
disk_retries  db 0
dap_count     dw 0
dap_off       dw 0
dap_seg       dw 0
dap_lba       dq 0
dap:
    dw EDD_PACKET_SIZE
    dw 0
    dw 0
    dw 0
    dq 0
