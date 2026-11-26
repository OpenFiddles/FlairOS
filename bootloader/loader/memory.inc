; memory.inc - A20 gate and the firmware E820 memory map.

; Check A20 by testing the wrap-around between 0000:0500 and FFFF:0510.
; Returns AL = 1 when the gate is enabled.
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
    mov al, 1
    je .disabled
    ret
.disabled:
    xor al, al
    ret

; Try the BIOS gate, then fast A20 on port 0x92, then the 8042 controller.
enable_a20:
    call check_a20
    test al, al
    jnz .ok

    mov ax, 0x2401
    int 0x15
    call check_a20
    test al, al
    jnz .ok

    in al, 0x92
    or al, 2
    and al, 0xFE
    out 0x92, al
    call check_a20
    test al, al
    jnz .ok

    call .kbc_wait_in
    mov al, 0xD1
    out 0x64, al
    call .kbc_wait_in
    mov al, 0xDF
    out 0x60, al
    call .kbc_wait_in
.ok:
    ret
.kbc_wait_in:
    in al, 0x64
    test al, 2
    jnz .kbc_wait_in
    ret

; Collect the E820 map into the boot-info block and record how many entries
; the kernel may read. Zero-length entries are skipped; the walk stops after
; BI_E820_MAX entries so the block can never overrun the loader's stack.
collect_e820:
    mov dword [BOOT_INFO_ADDR + BI_E820_COUNT], 0
    xor ax, ax
    mov es, ax
    xor ebx, ebx
    mov di, E820_ENTRIES
.entry:
    cmp dword [BOOT_INFO_ADDR + BI_E820_COUNT], BI_E820_MAX
    jae .done
    mov eax, 0xE820
    mov edx, 0x534D4150
    mov ecx, 24
    int 0x15
    jc .done
    cmp eax, 0x534D4150
    jne .done
    mov eax, [di + 8]           ; length low
    or eax, [di + 12]           ; length high
    jz .next
    add di, 24
    inc dword [BOOT_INFO_ADDR + BI_E820_COUNT]
.next:
    test ebx, ebx
    jnz .entry
.done:
    ret
