cat > isr.s <<'EOF'
; isr.s - IDT placeholder + PIC remap + irq1 wrapper
BITS 32
global install_idt_and_pic, irq1_handler_asm, pic_send_eoi, isr_common_handler

section .data
idt_ptr:
    dw 0
    dd 0

section .bss
idt: resb 256*8

section .text
extern keyboard_handler_c

install_idt_and_pic:
    pusha

    ; idt_ptr: size-1 then base
    mov eax, idt
    mov ebx, eax
    add ebx, 256*8
    dec ebx
    mov word [idt_ptr], bx
    mov dword [idt_ptr+2], eax

    ; remap PICs (master 0x20, slave 0x28)
    mov al, 0x11
    out 0x20, al
    out 0xa0, al

    mov al, 0x20
    out 0x21, al
    mov al, 0x28
    out 0xa1, al

    mov al, 0x04
    out 0x21, al
    mov al, 0x02
    out 0xa1, al

    mov al, 0x01
    out 0x21, al
    out 0xa1, al

    mov al, 0
    out 0x21, al
    out 0xa1, al

    ; load idt
    lea eax, [idt_ptr]
    lidt [eax]

    popa
    ret

pic_send_eoi:
    push ebp
    mov ebp, esp
    mov al, 0x20
    out 0x20, al
    pop ebp
    ret

; IRQ1 wrapper: push regs, call C handler, send EOI, iret
irq1_handler_asm:
    cli
    pusha
    call isr_common_handler
    mov al, 0x20
    out 0x20, al
    popa
    sti
    iret

isr_common_handler:
    push ebp
    mov ebp, esp
    call keyboard_handler_c
    pop ebp
    ret
EOF