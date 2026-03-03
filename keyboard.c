cat > keyboard.c <<'EOF'
#include <stdint.h>
#include "kprint.h"

static inline uint8_t inb(uint16_t port) {
    uint8_t ret;
    __asm__ volatile ("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

static const char scmap[128] = {
  0,  27,'1','2','3','4','5','6','7','8','9','0','-','=', '\b',
  '\t','q','w','e','r','t','y','u','i','o','p','[',']','\n',
  0,'a','s','d','f','g','h','j','k','l',';','\'','`',0,
  '\\','z','x','c','v','b','n','m',',','.','/',0,'*',0,
  ' ',
};

extern void kprint(const char*);

void keyboard_handler_c(void) {
    uint8_t sc = inb(0x60);
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
EOF