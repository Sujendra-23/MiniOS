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
    mov esp, ebx
    popa
    add esp, 8
    iretd
section .rodata
global isr_table
isr_table:
%assign i 0
%rep 48
    dd isr%+i
%assign i i+1
%endrep
section .note.GNU-stack noalloc noexec nowrite progbits
