OUTPUT_FORMAT("elf32-i386")
ENTRY(kernel_entry)
SECTIONS {
    . = 0x00100000;
    .multiboot : { KEEP(*(.multiboot)) }
    .text ALIGN(4096) : {
        __text_start = .;
        *(.text*)
        . = ALIGN(4096);
        __text_end = .;
    }
    .rodata ALIGN(4096) : {
        __rodata_start = .;
        *(.rodata*)
        . = ALIGN(4096);
        __rodata_end = .;
    }
    .data ALIGN(4096) : { *(.data*) }
    .bss ALIGN(4096) : { *(COMMON) *(.bss*) }
    __kernel_end = .;
    ASSERT(__kernel_end <= 0x00400000, "Kernel exceeds identity paging window")
    /DISCARD/ : { *(.eh_frame*) *(.comment) }
}
