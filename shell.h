#ifndef SHELL_H
#define SHELL_H
/* Serial command shell. shell_poll runs in task 0 and consumes the UART RX ring. */
void shell_init(void);
void shell_poll(void);
#endif
