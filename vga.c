cat > vga.c <<'EOF'
#include <stdint.h>
#include "kprint.h"

static uint16_t* const VGA_BUF = (uint16_t*)0xB8000;
static int row = 0, col = 0;
static uint8_t color = 0x0F;

void kcls() {
    for (int r=0;r<25;r++) for (int c=0;c<80;c++) VGA_BUF[r*80 + c] = ((uint16_t)color << 8) | ' ';
    row = col = 0;
}

void kputc(char ch) {
    if (ch == '\n') { row++; col=0; }
    else {
        VGA_BUF[row*80 + col] = ((uint16_t)color << 8) | ch;
        col++;
        if (col >= 80) { col = 0; row++; }
    }
    if (row >= 25) {
        kcls();
    }
}

void kprint(const char* s) {
    for (const char* p = s; *p; ++p) kputc(*p);
}
EOF