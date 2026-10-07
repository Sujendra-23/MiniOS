#include <stdint.h>
#include "kprint.h"
#include "paging.h"

#include "io.h"

static const char scmap[128] = {
  0,  27,'1','2','3','4','5','6','7','8','9','0','-','=', '\b',
  '\t','q','w','e','r','t','y','u','i','o','p','[',']','\n',
  0,'a','s','d','f','g','h','j','k','l',';','\'','`',0,
  '\\','z','x','c','v','b','n','m',',','.','/',0,'*',0,
  ' ',
};

void keyboard_handler_c(void) {
    static int extended;
    uint8_t sc = inb(0x60);
    if (sc == 0xE0 || sc == 0xE1) { extended = 1; return; }
    if (extended) { extended = 0; return; }
    if (sc == 0x42) paging_test_null();
    if (sc == 0x43) paging_test_readonly();
    if (sc == 0x44) paging_test_unmapped();
    if (sc == 0x57) {
        __asm__ volatile ("movw $0x18, %%ax; movw %%ax, %%ds" : : : "eax");
    }
    if (sc == 0x58) __asm__ volatile ("ud2");
    if (sc & 0x80) {
        // key release - ignore
    } else {
        char ch = 0;
        if (sc < 128) ch = scmap[sc];
        if (ch) {
            char buf[2] = {ch, 0};
            kprint(buf);
        }
    }
}
