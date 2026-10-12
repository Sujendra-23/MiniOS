#ifndef FMT_H
#define FMT_H
#include <stdint.h>
/* Tiny buffer formatters: each appends to p and returns the new end.
 * Callers terminate with *p = 0 and print the buffer with kprint/uart_write. */
static inline char *f_str(char *p, const char *s) {
    while (*s) *p++ = *s++;
    return p;
}
static inline char *f_uint(char *p, uint32_t v) {
    char digits[10];
    unsigned n = 0;
    do { digits[n++] = '0' + v % 10; v /= 10; } while (v);
    while (n) *p++ = digits[--n];
    return p;
}
static inline char *f_hex(char *p, uint32_t v, unsigned digits) {
    static const char hex[] = "0123456789abcdef";
    while (digits) *p++ = hex[(v >> (4 * --digits)) & 0xF];
    return p;
}
static inline char *f_mac(char *p, const uint8_t mac[6]) {
    for (unsigned i = 0; i < 6; ++i) {
        if (i) *p++ = ':';
        p = f_hex(p, mac[i], 2);
    }
    return p;
}
static inline char *f_ip(char *p, const uint8_t ip[4]) {
    for (unsigned i = 0; i < 4; ++i) {
        if (i) *p++ = '.';
        p = f_uint(p, ip[i]);
    }
    return p;
}
#endif
