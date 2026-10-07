BITS 32
section .multiboot
align 4
dd 0x1BADB002
dd 0x00000003
dd -(0x1BADB002 + 0x00000003)

section .rodata
align 8
gdt:
    dq 0
    dq 0x00CF9A000000FFFF
    dq 0x00CF92000000FFFF
gdt_end:
gdt_ptr:
    dw gdt_end - gdt - 1
    dd gdt

section .bss
align 16
stack_bottom: resb 16384
stack_top:

section .text
global kernel_entry
extern kernel_main
kernel_entry:
    cli
    cld
    lgdt [gdt_ptr]
    jmp 0x08:.segments
.segments:
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    mov esp, stack_top
    xor ebp, ebp
    call kernel_main
.halt:
    cli
    hlt
    jmp .halt
section .note.GNU-stack noalloc noexec nowrite progbits
