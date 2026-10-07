#ifndef MEMORY_H
#define MEMORY_H
#include <stdbool.h>
#include "multiboot.h"
bool memory_init(uint32_t magic, const struct multiboot_info *info);
#endif
