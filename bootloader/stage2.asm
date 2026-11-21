; FlairOS stage 2: E820 memory map, boot menu, A20 verification, protected-mode handoff.
; FORCE_BOOT_MODE: 0 normal (interactive prompt), 1 LiveCD, 2 Setup media.
[BITS 16]
[ORG 0x9000]

%ifndef FORCE_BOOT_MODE
%define FORCE_BOOT_MODE 0
%endif

BOOT_INFO_ADDR    equ 0x4FF0
BOOT_INFO_MAGIC   equ 0x544F4F42
BOOT_INFO_VERSION equ 1
E820_COUNT        equ 0x5000
E820_ENTRIES      equ 0x5010
E820_MAX          equ 32

stage2_start:
    cld
    mov [boot_drive], dl
    xor ax, ax
    mov ds, ax
    mov es, ax

    mov si, msg_banner
    call print16

    ; Collect BIOS E820 memory map at 0x5000 / 0x5010.
    mov dword [E820_COUNT], 0
    xor ebx, ebx
    mov di, E820_ENTRIES
.e820:
    mov eax, 0xE820
    mov edx, 0x534D4150
    mov ecx, 24
    int 0x15
    jc .e820_done
    cmp eax, 0x534D4150
    jne .e820_done
    add di, 24
    inc dword [E820_COUNT]
    test ebx, ebx
    jz .e820_done
    cmp dword [E820_COUNT], E820_MAX
    jb .e820
.e820_done:

%if FORCE_BOOT_MODE == 0
    call select_boot_mode
%endif

    ; Versioned handoff sits directly before the E820 buffer.
    mov dword [BOOT_INFO_ADDR], BOOT_INFO_MAGIC
    mov dword [BOOT_INFO_ADDR + 4], BOOT_INFO_VERSION
    movzx eax, byte [boot_mode_val]
    mov dword [BOOT_INFO_ADDR + 8], eax
    movzx eax, byte [boot_drive]
    mov dword [BOOT_INFO_ADDR + 12], eax

    call enable_a20

    cli
    lgdt [gdt_descriptor]
    mov eax, cr0
    or eax, 1
    mov cr0, eax
    jmp dword 0x08:protected_entry

%if FORCE_BOOT_MODE == 0
; Wait ~1.5s (27 BIOS ticks) for 1..5 to select boot mode; default is 0 (Normal).
select_boot_mode:
    mov si, msg_menu
    call print16
    xor ah, ah
    int 0x1A
    add dx, 27
    mov [deadline_lo], dx
.wait_key:
    mov ah, 0x01
    int 0x16
    jnz .got_key
    xor ah, ah
    int 0x1A
    mov ax, dx
    sub ax, [deadline_lo]
    js .wait_key
    jmp .done_menu
.got_key:
    xor ah, ah
    int 0x16
    cmp al, 13
    je .done_menu
    cmp al, '1'
    jb .wait_key
    cmp al, '5'
    ja .wait_key
    sub al, '1'
    mov [boot_mode_val], al
.done_menu:
    mov si, msg_nl
    call print16
    ret
%endif

; Check A20 by testing wrap-around between 0000:0500 and FFFF:0510.
check_a20:
    push ds
    push es
    xor ax, ax
    mov ds, ax
    mov ax, 0xFFFF
    mov es, ax
    mov al, [ds:0x0500]
    mov ah, [es:0x0510]
    push ax
    mov byte [ds:0x0500], 0x00
    mov byte [es:0x0510], 0xFF
    cmp byte [ds:0x0500], 0xFF
    pop ax
    mov [ds:0x0500], al
    mov [es:0x0510], ah
    pop es
    pop ds
    mov al, 0
    je .disabled
    mov al, 1
.disabled:
    ret

enable_a20:
    call check_a20
    test al, al
    jnz .a20_ok

    ; 1. BIOS A20 gate
    mov ax, 0x2401
    int 0x15
    call check_a20
    test al, al
    jnz .a20_ok

    ; 2. Fast A20 (port 0x92)
    in al, 0x92
    or al, 2
    and al, 0xFE
    out 0x92, al
    call check_a20
    test al, al
    jnz .a20_ok

    ; 3. 8042 keyboard controller fallback
    call .kbc_wait_in
    mov al, 0xD1
    out 0x64, al
    call .kbc_wait_in
    mov al, 0xDF
    out 0x60, al
    call .kbc_wait_in
.a20_ok:
    ret
.kbc_wait_in:
    in al, 0x64
    test al, 2
    jnz .kbc_wait_in
    ret

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

boot_drive    db 0
boot_mode_val db FORCE_BOOT_MODE
deadline_lo   dw 0
msg_banner    db "FlairOS Stage 2", 13, 10, 0
%if FORCE_BOOT_MODE == 0
msg_menu      db "[1] Normal [2] Live [3] Setup [4] Recovery [5] Diag > ", 0
msg_nl        db 13, 10, 0
%endif

gdt_start:
    dq 0
gdt_code:
    dw 0xFFFF, 0x0000
    db 0x00, 10011010b, 11001111b, 0x00
gdt_data:
    dw 0xFFFF, 0x0000
    db 0x00, 10010010b, 11001111b, 0x00
gdt_end:
gdt_descriptor:
    dw gdt_end - gdt_start - 1
    dd gdt_start

[BITS 32]
protected_entry:
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    mov esp, 0x90000
    mov eax, 0x10000
    jmp eax
