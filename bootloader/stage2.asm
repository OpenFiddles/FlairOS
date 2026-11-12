; Stage 2 bootloader: reads the BIOS memory map, sets up protected mode, A20, and jumps to kernel.
[BITS 16]
[ORG 0x9000]

; E820 map for the kernel: entry count at 0x5000, 24-byte entries from 0x5010.
E820_COUNT   equ 0x5000
E820_ENTRIES equ 0x5010
E820_MAX     equ 32

stage2_start:
    mov [boot_drive], dl

    ; Stage 1 leaves ES at 0x3000, so reset the segments before the BIOS call.
    xor ax, ax
    mov ds, ax
    mov es, ax

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

    ; Enable A20 Line via Fast A20 method (System Control Port A)
    in al, 0x92
    or al, 2
    out 0x92, al

    ; Switch to 32-bit Protected Mode
    cli
    lgdt [gdt_descriptor]
    mov eax, cr0
    or eax, 1
    mov cr0, eax
    jmp dword 0x08:protected_entry

boot_drive db 0

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

    ; Pass control to FlairOS Kernel Entry point at physical 0x10000
    mov eax, 0x10000
    jmp eax
