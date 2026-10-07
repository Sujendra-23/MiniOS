BITS 32
section .text
extern interrupt_dispatch
%macro ISR 1
global isr%1
isr%1:
%if %1 != 8 && %1 != 10 && %1 != 11 && %1 != 12 && %1 != 13 && %1 != 14 && %1 != 17 && %1 != 21 && %1 != 29 && %1 != 30
    push dword 0
%endif
    push dword %1
    jmp interrupt_common
%endmacro
%assign i 0
%rep 48
    ISR i
%assign i i+1
%endrep
interrupt_common:
    pusha
    cld
    mov ebx, esp
    and esp, -16
    sub esp, 12
    push ebx
    call interrupt_dispatch
    mov esp, eax               ; C selects the next task interrupt frame
    popa
    add esp, 8
    iretd
global scheduler_task_start
extern scheduler_task_entry
scheduler_task_start:
    xor ebp, ebp
    call scheduler_task_entry  ; New task ESP is 16-byte aligned before CALL.
.halt:
    cli
    hlt
    jmp .halt

section .rodata
global isr_table
isr_table:
%assign i 0
%rep 48
    dd isr%+i
%assign i i+1
%endrep
section .note.GNU-stack noalloc noexec nowrite progbits
