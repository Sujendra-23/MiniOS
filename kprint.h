#ifndef KPRINT_H
#define KPRINT_H
#include <stdint.h>
void kcls(void);
void kputc(char ch);
void kprint(const char *s);
void kprint_uint(uint32_t value);
void kprint_hex(uint32_t value);
void vga_ticks(uint32_t ticks);
#endif
