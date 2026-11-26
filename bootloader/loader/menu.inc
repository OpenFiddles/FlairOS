; menu.inc - the boot menu.
;
; FORCE_BOOT_MODE 0 shows the prompt with a ~1.5 s (27 BIOS tick) timeout and
; defaults to Normal. The live and setup images define 1 and 2 so they boot
; straight into that mode without a prompt.

%if FORCE_BOOT_MODE == 0
; Wait for 1..5, Enter to accept the default, or the timeout to expire.
select_boot_mode:
    mov si, msg_menu
    call puts16
    xor ah, ah
    int 0x1A                    ; DX = BIOS ticks since midnight
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
    mov al, [boot_mode_val]
    add al, '1'
    mov [mode_echo], al
    mov si, msg_mode
    call puts16
    mov si, msg_nl
    call puts16
    ret
%endif

%if FORCE_BOOT_MODE == 0
deadline_lo   dw 0
mode_echo     db 0
msg_menu      db "[1] Normal [2] Live [3] Setup [4] Recovery [5] Diag > ", 0
msg_mode      db "loader: boot mode ", 0
%endif

boot_mode_val db FORCE_BOOT_MODE
