#include <stdbool.h>
#include "shell.h"
#include "uart.h"
#include "frame.h"
#include "interrupts.h"
#include "scheduler.h"

#define LINE_MAX 240u
static char line[LINE_MAX + 1];
static unsigned length;

static bool starts_with(const char *s, const char *prefix) {
    while (*prefix) if (*s++ != *prefix++) return false;
    return true;
}
static bool equals(const char *a, const char *b) {
    while (*a && *a == *b) { ++a; ++b; }
    return *a == *b;
}
static void prompt(void) { uart_write("minios> "); }

static void run(char *command) {
    while (*command == ' ') ++command;
    if (!*command) return;
    if (equals(command, "help")) {
        uart_write("commands: help, uptime, sched, mem, uart, echo <text>\n");
    } else if (equals(command, "uptime")) {
        uint32_t ticks = interrupts_ticks();
        uart_write("uptime: ");
        uart_write_uint(ticks / 100);
        uart_write("s ticks=");
        uart_write_uint(ticks);
        uart_write("\n");
    } else if (equals(command, "sched")) {
        uart_write("sched: task=");
        uart_write_uint(scheduler_current_task());
        uart_write(" switches=");
        uart_write_uint(scheduler_switches());
        uart_write(" workers=");
        for (uint32_t i = 1; i < scheduler_task_count(); ++i) {
            if (i != 1) uart_write(",");
            uart_write_uint(scheduler_task_work(i));
        }
        uart_write("\n");
    } else if (equals(command, "mem")) {
        uart_write("mem: free_frames=");
        uart_write_uint(frame_free_count());
        uart_write(" free_kib=");
        uart_write_uint(frame_free_count() * (FRAME_SIZE / 1024));
        uart_write("\n");
    } else if (equals(command, "uart")) {
        struct uart_stats s = uart_stats();
        uart_write("uart: irqs=");
        uart_write_uint(s.irqs);
        uart_write(" rx=");
        uart_write_uint(s.rx);
        uart_write(" tx=");
        uart_write_uint(s.tx);
        uart_write(" rx_dropped=");
        uart_write_uint(s.rx_dropped);
        uart_write(" overruns=");
        uart_write_uint(s.overruns);
        uart_write("\n");
    } else if (equals(command, "echo") || starts_with(command, "echo ")) {
        uart_write(command[4] ? command + 5 : "");
        uart_write("\n");
    } else {
        uart_write("unknown command: ");
        uart_write(command);
        uart_write("\n");
    }
}

void shell_init(void) {
    uart_write("\nMiniOS serial shell on COM1. Type 'help'.\n");
    prompt();
}

void shell_poll(void) {
    char ch;
    while (uart_getc(&ch)) {
        if (ch == '\r' || ch == '\n') {
            uart_write("\n");
            line[length] = 0;
            run(line);
            length = 0;
            prompt();
        } else if (ch == '\b' || ch == 0x7F) {
            if (length) { --length; uart_write("\b \b"); }
        } else if (ch >= ' ' && ch < 0x7F && length < LINE_MAX) {
            line[length++] = ch;
            uart_putc(ch);
        }
    }
}
