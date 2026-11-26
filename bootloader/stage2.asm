; FlairOS stage 2 loader.
;
; Responsibilities, in order:
;   1. set up the console (BIOS teletype + COM1) and probe disk extensions
;   2. enable A20 and verify it
;   3. collect the firmware E820 memory map
;   4. ask for a boot mode (skipped for the live and setup media)
;   5. load every image listed in loader/images.inc
;   6. verify the kernel image header before trusting it
;   7. program a VBE linear framebuffer when the firmware offers one
;   8. publish a checksummed boot-info block and enter protected mode
;
; FORCE_BOOT_MODE: 0 normal (interactive prompt), 1 LiveCD, 2 Setup media.
[BITS 16]
[ORG 0x9000]

%ifndef FORCE_BOOT_MODE
%define FORCE_BOOT_MODE 0
%endif

; Only the constants may precede the entry point: NASM emits included code in
; place, and stage 1 jumps to the first byte of this image.
%include "bootinfo.inc"

stage2_start:
    cld
    mov [boot_drive], dl
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7C00
    sti

    call serial_init
    call disk_probe
    mov si, msg_banner
    call puts16

    call enable_a20
    call check_a20
    test al, al
    jz .a20_failed
    mov si, msg_a20
    call puts16
    jmp .memory

.a20_failed:
    mov si, msg_no_a20
    call puts16
    jmp boot_fail

.memory:
    call collect_e820
    mov si, msg_memory
    call puts16
    mov ax, [BOOT_INFO_ADDR + BI_E820_COUNT]
    call puthex16
    mov si, msg_nl
    call puts16

%if FORCE_BOOT_MODE == 0
    call select_boot_mode
%endif

    call load_images
    jc boot_fail
    mov si, msg_kernel_ok
    call puts16

    call vbe_init

    call build_boot_info
    jmp enter_protected

boot_fail:
    mov si, msg_halt
    call puts16
.hang:
    hlt
    jmp .hang

; Publish the boot protocol block the kernel reads at 0x4000. The checksum
; covers the fifteen header dwords; the E820 array that follows is firmware
; data and is not summed.
build_boot_info:
    mov dword [BOOT_INFO_ADDR + BI_MAGIC], BOOT_INFO_MAGIC
    mov dword [BOOT_INFO_ADDR + BI_VERSION], BOOT_INFO_VERSION
    movzx eax, byte [boot_mode_val]
    mov [BOOT_INFO_ADDR + BI_MODE], eax
    movzx eax, byte [boot_drive]
    mov [BOOT_INFO_ADDR + BI_DRIVE], eax
    mov dword [BOOT_INFO_ADDR + BI_E820_ADDR], E820_ENTRIES
    mov dword [BOOT_INFO_ADDR + BI_KERNEL_LBA], KERNEL_LBA
    mov dword [BOOT_INFO_ADDR + BI_KERNEL_SEC], KERNEL_SECTORS
    mov dword [BOOT_INFO_ADDR + BI_KERNEL_LEN], KERNEL_SECTORS * 512
    mov eax, [vid_lfb]
    mov [BOOT_INFO_ADDR + BI_LFB], eax
    mov ax, [vid_width]
    mov [BOOT_INFO_ADDR + BI_WIDTH], ax
    mov ax, [vid_height]
    mov [BOOT_INFO_ADDR + BI_HEIGHT], ax
    mov ax, [vid_bpp]
    mov [BOOT_INFO_ADDR + BI_BPP], ax
    mov ax, [vid_pitch]
    mov [BOOT_INFO_ADDR + BI_PITCH], ax
    movzx eax, word [vid_ver]
    mov [BOOT_INFO_ADDR + BI_VBE_VER], eax
    mov eax, [vid_vram]
    mov [BOOT_INFO_ADDR + BI_VRAM_KB], eax

    mov dword [BOOT_INFO_ADDR + BI_FLAGS], BOOT_FLAG_SERIAL
    cmp byte [edd_ok], 0
    je .no_edd
    or dword [BOOT_INFO_ADDR + BI_FLAGS], BOOT_FLAG_EDD
.no_edd:
    cmp dword [vid_lfb], 0
    je .no_video
    or dword [BOOT_INFO_ADDR + BI_FLAGS], BOOT_FLAG_VIDEO
.no_video:
    xor eax, eax
    mov si, BOOT_INFO_ADDR
    mov cx, BI_CHECKSUM / 4
.sum:
    add eax, [si]
    add si, 4
    loop .sum
    mov [BOOT_INFO_ADDR + BI_CHECKSUM], eax
    ret

enter_protected:
    cli
    lgdt [gdt_descriptor]
    mov eax, cr0
    or eax, 1
    mov cr0, eax
    jmp dword 0x08:protected_entry

msg_banner      db "FlairOS loader", 13, 10, 0
msg_a20         db "loader: A20 gate enabled", 13, 10, 0
msg_no_a20      db "loader: A20 gate could not be enabled", 13, 10, 0
msg_memory      db "loader: e820 entries=", 0
msg_kernel_ok   db "loader: kernel image verified", 13, 10, 0
msg_halt        db "loader: boot stopped", 13, 10, 0
msg_nl          db 13, 10, 0

; Helper modules. They are assembled 16-bit, so they stay below the protected
; mode stub. Their data (boot_drive, dap, message strings) lives here too.
%include "loader/serial.inc"
%include "loader/disk.inc"
%include "loader/edd.inc"
%include "loader/memory.inc"
%include "loader/menu.inc"
%include "loader/images.inc"
%include "loader/video.inc"

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
    mov eax, KERNEL_BASE
    jmp eax
