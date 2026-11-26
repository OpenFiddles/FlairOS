; SPDX-License-Identifier: AGPL-3.0-only
; FlairOS 64-bit stage-2 loader.
;
; Reads the flat kernel image off disk sectors into 0x10000, enables A20,
; switches to 32-bit protected mode, copies the kernel to its link address at
; 0x100000 and jumps to the kernel's own 32 -> 64-bit entry.
;
; The kernel's entry trampoline (arch/x86_64/boot.S) sets up long mode, the
; identity map and BSS itself, so this loader only has to deliver the image in
; memory. The values patched by tools/make_bootdisk.py say which sectors to read.

[BITS 16]
[ORG 0x8000]

KERNEL_STAGE equ 0x10000
KERNEL_LOAD  equ 0x100000
MAX_CHUNK    equ 64                       ; sectors per BIOS call (32 KiB)

start:
    cli
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7C00
    sti
    mov [drive], dl

    call serial_init
    mov si, msg_banner
    call puts

    mov si, msg_lba
    call puts
    mov eax, [kernel_lba]
    call puthex32
    mov si, msg_sectors
    call puts
    mov eax, [kernel_sectors]
    call puthex32
    mov si, msg_entry
    call puts
    mov eax, [kernel_entry]
    call puthex32
    call putnl

    mov ecx, [kernel_sectors]
    or  ecx, ecx
    jz  read_failed
    mov bx, KERNEL_STAGE >> 4              ; destination segment
    mov edi, [kernel_lba]

.read_loop:
    test ecx, ecx
    jz  .copied
    mov eax, MAX_CHUNK
    cmp ecx, eax
    jae .have_count
    mov eax, ecx
.have_count:
    mov [dap_count], ax
    mov [dap_seg], bx
    mov word [dap_off], 0
    mov [dap_lba], edi

    push cx
    push bx
    push edi
    mov si, dap
    mov ah, 0x42
    mov dl, [drive]
    int 0x13
    pop edi
    pop bx
    pop cx
    jc  read_failed

    sub ecx, eax
    add edi, eax
    shl eax, 5                             ; sector -> paragraph advance
    add bx, ax
    jmp .read_loop

.copied:
    mov si, msg_staged
    call puts
    jmp enable_a20

read_failed:
    jc .report
    mov si, msg_read_fail
    call puts
    jmp halt
.report:
    mov si, msg_read_error
    call puts
    mov al, ah
    call puthex8
    call putnl
    jmp halt

enable_a20:
    ; Fast A20 through port 92h, then the keyboard controller as a fallback.
    in  al, 0x92
    or  al, 0x02
    and al, 0xFE
    out 0x92, al

    cli
    call kbc_a20
    call a20_verify
    test al, al
    jnz .a20_ok
    mov si, msg_a20_fail
    call puts
    jmp halt
.a20_ok:
    mov si, msg_a20
    call puts

    lgdt [gdt_desc]
    mov eax, cr0
    or  eax, 1
    mov cr0, eax
    ; Far jump into the 32-bit code segment (explicit 32-bit offset).
    db 0x66, 0xEA
    dd .pm32
    dw 0x08

[BITS 32]
.pm32:
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    mov esp, 0x7C00

    mov esi, KERNEL_STAGE
    mov edi, KERNEL_LOAD
    mov ecx, [kernel_sectors]
    shl ecx, 7                             ; sectors * 512 / 4
    cld
    rep movsd

    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    xor eax, eax
    xor ebx, ebx
    ; The kernel entry is not necessarily the link base: it sits after the
    ; Multiboot header, so the image builder patches the ELF entry here.
    jmp dword [kernel_entry]

[BITS 16]
halt:
    cli
.hang:
    hlt
    jmp .hang

kbc_a20:
    call kbc_wait_in
    mov al, 0xAD
    out 0x64, al
    call kbc_wait_in
    mov al, 0xD0
    out 0x64, al
    call kbc_wait_out
    in  al, 0x60
    or  al, 0x02
    mov bl, al
    call kbc_wait_in
    mov al, 0xD1
    out 0x64, al
    call kbc_wait_in
    mov al, bl
    out 0x60, al
    call kbc_wait_in
    mov al, 0xAE
    out 0x64, al
    ret

kbc_wait_in:
    in  al, 0x64
    test al, 0x02
    jnz kbc_wait_in
    ret

kbc_wait_out:
    in  al, 0x64
    test al, 0x01
    jz  kbc_wait_out
    ret

; Confirm the gate really moved: 0x0:0x500 and 0xFFFF:0x510 alias when A20 is
; closed, so writing 0xFF through the high alias must leave 0x00 below.
; Returns AL=1 when A20 is open.
a20_verify:
    push ds
    push es
    push bx
    push si
    push di
    xor ax, ax
    mov ds, ax
    mov ax, 0xFFFF
    mov es, ax
    mov si, 0x0500
    mov di, 0x0510
    mov byte [si], 0x00
    mov byte [es:di], 0xFF
    mov al, [si]
    cmp al, 0x00
    je  .open
    xor al, al
    jmp .done
.open:
    mov al, 1
.done:
    pop di
    pop si
    pop bx
    pop es
    pop ds
    ret

; --- serial console (COM1, 115200 8N1) -------------------------------------
serial_init:
    mov dx, 0x3F9
    mov al, 0
    out dx, al
    mov dx, 0x3FB
    mov al, 0x80
    out dx, al
    mov dx, 0x3F8
    mov al, 1
    out dx, al
    mov dx, 0x3F9
    xor al, al
    out dx, al
    mov dx, 0x3FB
    mov al, 0x03
    out dx, al
    mov dx, 0x3FA
    mov al, 0xC7
    out dx, al
    mov dx, 0x3FC
    mov al, 0x0B
    out dx, al
    ret

putc:
    push ax
    push dx
.wait:
    mov dx, 0x3FD
    in  al, dx
    test al, 0x20
    jz  .wait
    pop dx
    pop ax
    mov dx, 0x3F8
    out dx, al
    ret

puts:
    push ax
.next:
    lodsb
    test al, al
    jz  .done
    call putc
    jmp .next
.done:
    pop ax
    ret

putnl:
    mov al, 13
    call putc
    mov al, 10
    call putc
    ret

puthex8:
    push ax
    mov ah, al
    shr al, 4
    call .nib
    mov al, ah
    call .nib
    pop ax
    ret
.nib:
    and al, 0x0F
    cmp al, 10
    jb  .digit
    add al, 'a' - 10
    jmp putc
.digit:
    add al, '0'
    jmp putc

puthex32:
    push eax
    shr eax, 16
    call puthex16
    pop eax
puthex16:
    push eax
    shr eax, 8
    call puthex8
    pop eax
    call puthex8
    ret

drive: db 0
align 4
hdr_magic:      db "F64HDR00"
kernel_lba:     dd 0
kernel_sectors: dd 0
kernel_entry:   dd 0

msg_banner:     db "FlairOS 64-bit loader", 13, 10, 0
msg_lba:        db "loader: kernel lba=", 0
msg_sectors:    db " sectors=", 0
msg_entry:      db " entry=", 0
msg_staged:     db "loader: kernel staged, A20 check", 13, 10, 0
msg_a20:        db "loader: A20 open, entering protected mode", 13, 10, 0
msg_a20_fail:   db "loader: A20 did not open", 13, 10, 0
msg_read_fail:  db "loader: no kernel image on disk", 13, 10, 0
msg_read_error: db "loader: disk read error ah=", 0

align 8
gdt:
    dq 0
    dq 0x00CF9A000000FFFF                  ; 0x08: flat 32-bit code, DPL0
    dq 0x00CF92000000FFFF                  ; 0x10: flat 32-bit data, DPL0
gdt_desc:
    dw gdt_desc - gdt - 1
    dd gdt

align 4
dap:
    db 0x10
    db 0
dap_count: dw 0
dap_off:   dw 0
dap_seg:   dw 0
dap_lba:   dq 0

times 4096 - ($ - $$) db 0
