#ifndef MULTIBOOT_H
#define MULTIBOOT_H
#include <stdint.h>
#define MULTIBOOT_BOOT_MAGIC 0x2BADB002u
struct multiboot_info {
    uint32_t flags, mem_lower, mem_upper, boot_device, cmdline;
    uint32_t mods_count, mods_addr, syms[4], mmap_length, mmap_addr;
    uint32_t drives_length, drives_addr, config_table, boot_loader_name;
    uint32_t apm_table, vbe_control_info, vbe_mode_info;
    uint16_t vbe_mode, vbe_interface_seg, vbe_interface_off, vbe_interface_len;
} __attribute__((packed));
struct multiboot_mmap {
    uint32_t size;
    uint64_t base, length;
    uint32_t type;
} __attribute__((packed));
struct multiboot_module {
    uint32_t start, end, string, reserved;
};
#endif
