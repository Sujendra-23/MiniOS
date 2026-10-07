#include "memory.h"
#include "frame.h"
extern char __kernel_end[];

static bool memory_map_pass(const struct multiboot_info *info, bool reserve) {
    uint32_t offset = 0;
    while (offset < info->mmap_length) {
        uint32_t remaining = info->mmap_length - offset;
        if (remaining < sizeof(uint32_t)) return false;
        const struct multiboot_mmap *entry =
            (const struct multiboot_mmap *)(info->mmap_addr + offset);
        if (entry->size < 20 || entry->size > remaining - 4) return false;
        if (!reserve && entry->type == 1)
            frame_mark_available(entry->base, entry->length);
        if (reserve && entry->type != 1)
            frame_reserve(entry->base, entry->length);
        offset += entry->size + 4;
    }
    return true;
}
bool memory_init(uint32_t magic, const struct multiboot_info *info) {
    if (magic != MULTIBOOT_BOOT_MAGIC || !info) return false;
    frame_init();
    if (info->flags & (1u << 6)) {
        if ((uint64_t)info->mmap_addr + info->mmap_length > UINT32_MAX ||
            !memory_map_pass(info, false) || !memory_map_pass(info, true))
            return false;
    } else if (info->flags & 1) {
        frame_mark_available(0x100000, (uint64_t)info->mem_upper * 1024);
    } else {
        return false;
    }
    /* Firmware, VGA, boot stack, kernel, bitmaps, and static paging structures. */
    frame_reserve(0, (uint32_t)__kernel_end);
    frame_reserve((uint32_t)info, sizeof(*info));
    if (info->flags & (1u << 6))
        frame_reserve(info->mmap_addr, info->mmap_length);
    if (info->flags & (1u << 3)) {
        const struct multiboot_module *modules =
            (const struct multiboot_module *)info->mods_addr;
        uint64_t bytes = (uint64_t)info->mods_count * sizeof(*modules);
        if ((uint64_t)info->mods_addr + bytes > UINT32_MAX) return false;
        frame_reserve(info->mods_addr, bytes);
        for (uint32_t i = 0; i < info->mods_count; ++i) {
            if (modules[i].end < modules[i].start) return false;
            frame_reserve(modules[i].start, modules[i].end - modules[i].start);
        }
    }
    return frame_free_count() != 0;
}
