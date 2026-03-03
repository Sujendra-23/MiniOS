cat > linker.ld <<'EOF'
OUTPUT_FORMAT("elf32-i386")
ENTRY(kernel_entry)

SECTIONS
{
  . = 0x00100000;

  .text : {
    *(.multiboot)
    *(.text)
  }

  .rodata : { *(.rodata) }

  .data : { *(.data) }

  .bss : { *(.bss COMMON) }
}
EOF