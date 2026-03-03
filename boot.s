cat > boot.s <<'EOF'
; boot.s - multiboot header and entry (32-bit)
BITS 32
align 4
section .multiboot
  dd 0x1BADB002
  dd 0x00010003
  dd - (0x1BADB002 + 0x00010003)

section .text
global kernel_entry
extern kernel_main

kernel_entry:
    cli
    mov esp, 0x90000
    call kernel_main
    hlt_loop:
        hlt
        jmp hlt_loop
EOF