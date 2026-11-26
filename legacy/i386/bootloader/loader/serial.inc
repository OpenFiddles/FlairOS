; serial.inc - COM1 output so a headless boot is observable.
;
; The kernel keeps printing to the same port (kernel/core/kernel.c), so the
; whole boot from boot sector to desktop appears on one -serial capture.

COM1 equ 0x03F8

; Configure COM1 for 115200 8N1 with the FIFO enabled.
serial_init:
    push ax
    push dx
    mov dx, COM1 + 1
    xor al, al
    out dx, al                  ; no UART interrupts
    mov dx, COM1 + 3
    mov al, 0x80
    out dx, al                  ; DLAB on
    mov dx, COM1 + 0
    mov al, 1
    out dx, al                  ; divisor 1 = 115200 baud
    mov dx, COM1 + 1
    xor al, al
    out dx, al
    mov dx, COM1 + 3
    mov al, 0x03
    out dx, al                  ; 8 data bits, no parity, one stop bit
    mov dx, COM1 + 2
    mov al, 0xC7
    out dx, al                  ; FIFO enable, clear, 14-byte threshold
    pop dx
    pop ax
    ret

; AL = byte to transmit.
serial_putc:
    push ax
    push dx
    mov ah, al
    mov dx, COM1 + 5
.wait:
    in al, dx
    test al, 0x20
    jz .wait
    mov dx, COM1
    mov al, ah
    out dx, al
    pop dx
    pop ax
    ret

; SI = NUL-terminated string, written to the BIOS console and the serial port.
puts16:
    push ax
    push bx
    push si
.next:
    lodsb
    test al, al
    jz .done
    call serial_putc
    mov ah, 0x0E
    mov bx, 0x0007
    int 0x10
    jmp .next
.done:
    pop si
    pop bx
    pop ax
    ret

; AL = value, printed as two hex digits on the serial port.
putbyte:
    push ax
    push cx
    mov cl, al
    mov al, cl
    shr al, 4
    call .digit
    mov al, cl
    call .digit
    pop cx
    pop ax
    ret
.digit:
    and al, 0x0F
    add al, '0'
    cmp al, '9'
    jbe .emit
    add al, 7
.emit:
    call serial_putc
    ret

; AX = 16-bit value, printed as four hex digits on the serial port.
puthex16:
    push cx
    push ax
    mov cx, 4
.digit:
    rol ax, 4
    push ax
    and al, 0x0F
    add al, '0'
    cmp al, '9'
    jbe .emit
    add al, 7
.emit:
    call serial_putc
    pop ax
    loop .digit
    pop ax
    pop cx
    ret
