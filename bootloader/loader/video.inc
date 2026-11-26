; video.inc - VBE mode selection.
;
; The loader asks the VBE BIOS for a 32-bit linear-framebuffer mode and records
; the result in the boot-info block. The kernel still programs its own display
; (Bochs DISPI) because that is what the desktop expects, but if that fails it
; falls back to the framebuffer described here instead of panicking.

VBE_CONTROL  equ 0x2000             ; 512-byte controller info buffer
VBE_MODEINFO equ 0x2200             ; 256-byte mode info buffer

; VideoModePtr is a real-mode far pointer, so the list is walked through FS.
; Preferred sizes are tried in order; the first exact 32bpp LFB mode wins.
vbe_init:
    ; The BIOS writes through ES:DI, so both segments must be flat here: the
    ; image loader leaves ES on the last staged segment.
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov byte [vid_err], 0
    mov dword [vid_lfb], 0
    mov word [vid_width], 0
    mov word [vid_height], 0
    mov word [vid_bpp], 0
    mov word [vid_pitch], 0
    mov word [vid_ver], 0
    mov dword [vid_vram], 0

    mov dword [VBE_CONTROL], 0x32454256     ; request "VBE2" controller info
    mov ax, 0x4F00
    mov di, VBE_CONTROL
    int 0x10
    cmp ax, 0x004F
    jne .err1
    cmp dword [VBE_CONTROL], 0x41534556     ; "VESA"
    jne .err2

    mov ax, [VBE_CONTROL + 4]
    mov [vid_ver], ax
    mov ax, [VBE_CONTROL + 18]              ; total memory in 64 KiB blocks
    movzx eax, ax
    shl eax, 6                              ; -> KiB
    mov [vid_vram], eax

    mov ax, [VBE_CONTROL + 14]
    mov [vid_list_off], ax
    mov ax, [VBE_CONTROL + 16]
    mov [vid_list_seg], ax

    mov si, vid_targets
.target:
    mov ax, [si]
    test ax, ax
    jz .err3
    mov [vid_want_w], ax
    mov ax, [si + 2]
    mov [vid_want_h], ax
    push si
    call .scan
    pop si
    jnc .programmed
    add si, 4
    jmp .target

.programmed:
    mov si, msg_video
    call puts16
    mov ax, [vid_width]
    call puthex16
    mov al, 'x'
    call serial_putc
    mov ax, [vid_height]
    call puthex16
    mov si, msg_nl
    call puts16
    clc
    ret
.err1:
    mov byte [vid_err], 1
    jmp .none
.err2:
    mov byte [vid_err], 2
    jmp .none
.err3:
    mov byte [vid_err], 3
    jmp .none
.err4:
    mov byte [vid_err], 4
.none:
    mov si, msg_novideo
    call puts16
    mov al, [vid_err]
    call putbyte
    mov si, msg_nl
    call puts16
    stc
    ret

; Walk the mode list for vid_want_w x vid_want_h at 32bpp with a linear
; framebuffer. Sets the mode and fills the vid_* locals. CF = 1 when absent.
.scan:
    push si
    mov si, [vid_list_off]
.mode:
    mov ax, [vid_list_seg]
    mov fs, ax
    mov cx, [fs:si]
    cmp cx, 0xFFFF
    je .miss
    add si, 2
    push si
    mov ax, 0x4F01
    mov di, VBE_MODEINFO
    int 0x10
    pop si
    cmp ax, 0x004F
    jne .mode

    mov ax, [VBE_MODEINFO + 0]              ; attributes
    and ax, 0x0091                          ; supported | graphics | LFB
    cmp ax, 0x0091
    jne .mode
    cmp byte [VBE_MODEINFO + 0x19], 32
    jne .mode
    mov ax, [VBE_MODEINFO + 0x12]
    cmp ax, [vid_want_w]
    jne .mode
    mov ax, [VBE_MODEINFO + 0x14]
    cmp ax, [vid_want_h]
    jne .mode

    ; Program the mode. VBE 2.0+ selects the LFB with bit 14 and leaves the
    ; display memory alone with bit 15; VBE 1.2 uses bit 15 for the LFB.
    mov bx, cx
    cmp word [vid_ver], 0x0200
    jb .legacy_lfb
    or bx, 0x4000
    jmp .set
.legacy_lfb:
    or bx, 0x8000
.set:
    mov ax, 0x4F02
    int 0x10
    cmp ax, 0x004F
    jne .err4

    ; The mode info from the 4F01 query above still describes the mode that
    ; 4F02 just programmed, so the framebuffer fields are read from it.
    mov ax, [VBE_MODEINFO + 0x10]
    mov [vid_pitch], ax
    mov ax, [VBE_MODEINFO + 0x12]
    mov [vid_width], ax
    mov ax, [VBE_MODEINFO + 0x14]
    mov [vid_height], ax
    mov al, [VBE_MODEINFO + 0x19]
    mov [vid_bpp], al
    mov eax, [VBE_MODEINFO + 0x28]
    mov [vid_lfb], eax
    test eax, eax
    jz .miss
    pop si
    clc
    ret
.miss:
    pop si
    stc
    ret

vid_targets  dw 1024, 768, 800, 600, 0, 0
vid_err      db 0
vid_list_off dw 0
vid_list_seg dw 0
vid_want_w   dw 0
vid_want_h   dw 0
vid_lfb      dd 0
vid_width    dw 0
vid_height   dw 0
vid_bpp      dw 0
vid_pitch    dw 0
vid_ver      dw 0
vid_vram     dd 0
msg_video    db 13, 10, "loader: VBE ", 0
msg_novideo  db "loader: no VBE LFB; kernel draws", 13, 10, 0
