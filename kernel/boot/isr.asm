[BITS 32]
section .text
global isr_timer
global isr_keyboard
global isr_fault
global isr_syscall
global isr_stub_table

extern timer_irq
extern keyboard_irq
extern cpu_fault
extern airos_dispatch

isr_timer:
    pushad          ; Push EAX, ECX, EDX, EBX, ESP, EBP, ESI, EDI
    
    ; Mandatory for C calling convention (cdecl)
    cld             ; Clear direction flag (ensures string functions move forward)
    
    call timer_irq
    
    ; Send End-of-Interrupt (EOI) to the PIC
    mov al, 0x20
    out 0x20, al    ; Master PIC Command Port
    
    popad
    iretd

isr_keyboard:
    pushad
    cld
    
    call keyboard_irq
    
    ; Send End-of-Interrupt (EOI) to the PIC
    mov al, 0x20
    out 0x20, al    ; Master PIC Command Port
    
    popad
    iretd

; The CPU pushes an error code for some vectors only. Giving every vector its
; own stub normalises the frame to [vector][error][eip][cs][eflags] so the
; panic can name the exception instead of guessing.
%macro ISR_STUB_NOERR 1
isr_stub_%1:
    push dword 0
    push dword %1
    jmp isr_fault
%endmacro

%macro ISR_STUB_ERR 1
isr_stub_%1:
    push dword %1
    jmp isr_fault
%endmacro

ISR_STUB_NOERR 0
ISR_STUB_NOERR 1
ISR_STUB_NOERR 2
ISR_STUB_NOERR 3
ISR_STUB_NOERR 4
ISR_STUB_NOERR 5
ISR_STUB_NOERR 6
ISR_STUB_NOERR 7
ISR_STUB_ERR   8
ISR_STUB_NOERR 9
ISR_STUB_ERR   10
ISR_STUB_ERR   11
ISR_STUB_ERR   12
ISR_STUB_ERR   13
ISR_STUB_ERR   14
ISR_STUB_NOERR 15
ISR_STUB_NOERR 16
ISR_STUB_ERR   17
ISR_STUB_NOERR 18
ISR_STUB_NOERR 19
ISR_STUB_NOERR 20
ISR_STUB_ERR   21
ISR_STUB_NOERR 22
ISR_STUB_NOERR 23
ISR_STUB_NOERR 24
ISR_STUB_NOERR 25
ISR_STUB_NOERR 26
ISR_STUB_NOERR 27
ISR_STUB_NOERR 28
ISR_STUB_ERR   29
ISR_STUB_ERR   30
ISR_STUB_NOERR 31

section .rodata
isr_stub_table:
%assign i 0
%rep 32
    dd isr_stub_ %+ i
%assign i i+1
%endrep

section .text
isr_fault:
    cli             ; Disable interrupts permanently for panic state
    push esp        ; frame: vector, error code, EIP, CS, EFLAGS
    call cpu_fault  ; void cpu_fault(uint32_t *frame);
    
    ; If cpu_fault returns, securely halt
.halt:
    hlt
    jmp .halt

isr_syscall:
    pushad
    cld
    
    ; Pass the pointer to all saved registers to C; airos_dispatch tries the window
    ; and filesystem handlers first, then the base syscall table.
    push esp        
    call airos_dispatch
    add esp, 4
    
    ; EAX is the standard return register in C/x86. 
    ; We overwrite the saved EAX on the stack so popad restores the return value.
    mov [esp + 28], eax 
    
    popad
    iretd

global isr_mouse
extern mouse_irq_handler

isr_mouse:
    pushad
    cld
    call mouse_irq_handler
    mov al, 0x20
    out 0xA0, al
    out 0x20, al
    popad
    iretd

global task_switch_asm
task_switch_asm:
    mov eax, [esp + 4]
    mov edx, [esp + 8]
    push ebp
    push ebx
    push esi
    push edi
    mov [eax], esp
    mov esp, edx
    pop edi
    pop esi
    pop ebx
    pop ebp
    ret
