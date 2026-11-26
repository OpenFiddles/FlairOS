; images.inc - table-driven image loading and kernel verification.
;
; Every image lives at a fixed LBA on the boot floppy; the table is the single
; place that describes where each one goes. The kernel is verified before the
; handoff: its entry stub declares a magic, a header version and its own size,
; so a truncated or mis-staged image is refused instead of executed.

IMG_LBA     equ 0
IMG_SEG     equ 2
IMG_SECTORS equ 4
IMG_FLAGS   equ 6
IMG_SIZE    equ 8

IMG_OPTIONAL equ 1
IMG_KERNEL   equ 2

; Fixed disk extents. The boot filesystem occupies LBA 398..2446, so the
; staged images sit above it; tests/boot_layout_test.py guards these values.
APP_LBA          equ 263
APP_SECTORS      equ 128
EXPLORER_LBA     equ 2447
EXPLORER_SECTORS equ 32
TERMINAL_LBA     equ 2479
TERMINAL_SECTORS equ 64
SETTINGS_LBA     equ 2543
SETTINGS_SECTORS equ 32
NOTES_LBA        equ 2575
NOTES_SECTORS    equ 16
DESKTOP_LBA      equ 2591
DESKTOP_SECTORS  equ 16
PYTHON_LBA       equ 2623
PYTHON_SECTORS   equ 257

;      LBA             segment  sectors           flags
image_table:
    dw KERNEL_LBA,     0x1000, KERNEL_SECTORS,   IMG_KERNEL
    dw APP_LBA,        0x4000, APP_SECTORS,      0            ; built-in app
    dw EXPLORER_LBA,   0x9000, EXPLORER_SECTORS, 0            ; explorer.airp
    dw TERMINAL_LBA,   0x3800, TERMINAL_SECTORS, 0            ; terminal.airp
    dw SETTINGS_LBA,   0x3000, SETTINGS_SECTORS, 0            ; settings.airp
    dw NOTES_LBA,      0x3400, NOTES_SECTORS,    0            ; notes.airp
    dw DESKTOP_LBA,    0x9400, DESKTOP_SECTORS,  0            ; FlairDesktop.airp
    dw PYTHON_LBA,     0x6000, PYTHON_SECTORS,   IMG_OPTIONAL ; MicroPython
    dw 0

; Load every image in the table. CF = 1 on a read or verification failure.
load_images:
    mov si, image_table
.next:
    mov ax, [si + IMG_LBA]
    test ax, ax
    jz .done
    mov [img_lba], ax
    mov ax, [si + IMG_SEG]
    mov [img_seg], ax
    mov ax, [si + IMG_SECTORS]
    mov [img_left], ax
    mov al, [si + IMG_FLAGS]
    mov [img_flags], al
    mov [img_ptr], si

    test byte [img_flags], IMG_OPTIONAL
    jz .read
    ; Optional slot: read one sector and only continue for an AIRP image.
    mov ax, [img_lba]
    mov cx, 1
    mov dx, [img_seg]
    xor bx, bx
    call disk_read
    jc .read_error
    mov ax, [img_seg]
    mov es, ax
    cmp dword [es:0], AIRP_MAGIC
    jne .advance
    dec word [img_left]
    inc word [img_lba]
    add word [img_seg], 32

.read:
    mov cx, [img_left]
    test cx, cx
    jz .loaded
    cmp cx, 64
    jbe .chunk
    mov cx, 64
.chunk:
    mov ax, [img_lba]
    mov dx, [img_seg]
    xor bx, bx
    call disk_read
    jc .read_error
    sub [img_left], cx
    add [img_lba], cx
    mov ax, cx
    shl ax, 5                   ; 512 bytes per sector = 32 paragraphs
    add [img_seg], ax
    jmp .read

.loaded:
    mov si, [img_ptr]
    test byte [img_flags], IMG_KERNEL
    jz .advance
    call verify_kernel
    jc .bad_kernel
.advance:
    mov si, [img_ptr]
    add si, IMG_SIZE
    jmp .next
.done:
    clc
    ret

.bad_kernel:
    mov si, msg_bad_kernel
    call puts16
    stc
    ret

.read_error:
    mov si, msg_read_error
    call puts16
    mov ax, [img_lba]
    call puthex16
    mov si, msg_nl2
    call puts16
    stc
    ret

; The kernel image begins with a two-byte jump followed by a header:
;   +0  magic "FLRK"   +4  header version   +6  declared size in sectors
; Anything else means the wrong image (or garbage) is staged at 0x10000.
verify_kernel:
    push es
    mov ax, KERNEL_BASE >> 4            ; 16-bit offsets cannot reach 0x10000
    mov es, ax
    cmp byte [es:0], 0xEB               ; short jump over the header
    jne .bad
    cmp dword [es:2], KERNEL_MAGIC
    jne .bad
    cmp word [es:6], KERNEL_HDR_VER
    jne .bad
    mov ax, [es:8]
    test ax, ax
    jz .bad
    cmp ax, KERNEL_SECTORS
    ja .bad
    pop es
    clc
    ret
.bad:
    pop es
    stc
    ret

img_lba      dw 0
img_seg      dw 0
img_left     dw 0
img_flags    db 0
img_ptr      dw 0
msg_bad_kernel db "loader: bad kernel image", 13, 10, 0
msg_read_error db "loader: read error at 0x", 0
msg_nl2        db 13, 10, 0
