#include <stdbool.h>
#include "shell.h"
#include "uart.h"
#include "frame.h"
#include "interrupts.h"
#include "scheduler.h"
#include "pci.h"
#include "can.h"
#include "net.h"
#include "rt.h"
#include "fmt.h"

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

static void put_line(char *p) { *p++ = '\n'; *p = 0; }
static unsigned parse_count(const char *text, unsigned fallback) {
    if (!*text) return fallback;
    unsigned value = 0;
    for (; *text >= '0' && *text <= '9'; ++text) value = value * 10 + (unsigned)(*text - '0');
    return value > 1000 ? 1000 : value;
}
static void shell_can(const char *arg) {
    char buf[160], *p;
    unsigned controllers = can_controller_count();
    if (controllers < 2) {
        uart_write("can: need 2 controllers, found ");
        uart_write_uint(controllers);
        uart_write("\n");
        return;
    }
    unsigned frames = parse_count(arg, 64);
    struct can_selftest_result r = can_selftest(frames);
    p = f_str(buf, "can: controllers=2 sent=");
    p = f_uint(p, r.sent);
    p = f_str(p, " received=");
    p = f_uint(p, r.received);
    p = f_str(p, " mismatches=");
    p = f_uint(p, r.mismatches);
    p = f_str(p, " tx_timeouts=");
    p = f_uint(p, r.tx_timeouts);
    p = f_str(p, " rx_timeouts=");
    p = f_uint(p, r.rx_timeouts);
    put_line(p);
    uart_write(buf);
}
static void shell_net(void) {
    char buf[200], *p;
    const struct net_ctx *n = net_context();
    if (!net_present()) { uart_write("net: no RTL8139\n"); return; }
    p = f_str(buf, "net: mac=");
    p = f_mac(p, n->mac);
    p = f_str(p, " ip=");
    p = f_ip(p, n->ip);
    p = f_str(p, " rx=");
    p = f_uint(p, n->stats.rx_frames);
    p = f_str(p, " tx=");
    p = f_uint(p, n->stats.tx_frames);
    p = f_str(p, " dropped=");
    p = f_uint(p, n->stats.rx_dropped);
    put_line(p);
    uart_write(buf);
    p = f_str(buf, "net: arp_req_rx=");
    p = f_uint(p, n->stats.arp_req_rx);
    p = f_str(p, " arp_reply_tx=");
    p = f_uint(p, n->stats.arp_reply_tx);
    p = f_str(p, " arp_req_tx=");
    p = f_uint(p, n->stats.arp_req_tx);
    p = f_str(p, " arp_reply_rx=");
    p = f_uint(p, n->stats.arp_reply_rx);
    p = f_str(p, " icmp_echo_rx=");
    p = f_uint(p, n->stats.icmp_echo_rx);
    p = f_str(p, " icmp_reply_tx=");
    p = f_uint(p, n->stats.icmp_reply_tx);
    p = f_str(p, " bad_checksum=");
    p = f_uint(p, n->stats.bad_checksum);
    put_line(p);
    uart_write(buf);
    for (unsigned i = 0; i < NET_ARP_ENTRIES; ++i) {
        if (!n->arp[i].valid) continue;
        p = f_str(buf, "net: arp ");
        p = f_ip(p, n->arp[i].ip);
        p = f_str(p, " is-at ");
        p = f_mac(p, n->arp[i].mac);
        put_line(p);
        uart_write(buf);
    }
}
static void shell_rt_report(void) {
    char buf[200], *p;
    struct rt_tick_report t = rt_tick_report();
    p = f_str(buf, "rt: policy=");
    p = f_str(p, scheduler_policy() == SCHED_POLICY_FP ? "fp" : "rr");
    p = f_str(p, " tick_samples=");
    p = f_uint(p, t.samples);
    p = f_str(p, " tick_pit_min=");
    p = f_uint(p, t.min_pit);
    p = f_str(p, " tick_pit_max=");
    p = f_uint(p, t.max_pit);
    p = f_str(p, " tick_jitter_pit=");
    p = f_uint(p, t.max_pit - t.min_pit);
    p = f_str(p, " tick_jitter_us=");
    p = f_uint(p, rt_pit_to_ns(t.max_pit - t.min_pit) / 1000);
    put_line(p);
    uart_write(buf);
    for (unsigned i = 0; i < RT_TASK_COUNT; ++i) {
        struct rt_report r;
        if (!rt_report_for(i, &r)) continue;
        p = f_str(buf, "rt");
        p = f_uint(p, i);
        p = f_str(p, ": prio=");
        p = f_uint(p, r.priority);
        p = f_str(p, " period=");
        p = f_uint(p, r.period_ticks);
        p = f_str(p, " exec=");
        p = f_uint(p, r.exec_ticks);
        p = f_str(p, " released=");
        p = f_uint(p, r.releases);
        p = f_str(p, " done=");
        p = f_uint(p, r.completions);
        p = f_str(p, " deadline_miss=");
        p = f_uint(p, r.deadline_misses);
        p = f_str(p, " resp_max_ticks=");
        p = f_uint(p, r.max_response_ticks);
        p = f_str(p, " start_lat_pit_min=");
        p = f_uint(p, r.latency_min_pit);
        p = f_str(p, " max=");
        p = f_uint(p, r.latency_max_pit);
        p = f_str(p, " samples=");
        p = f_uint(p, r.latency_samples);
        put_line(p);
        uart_write(buf);
    }
}
static void prompt(void) { uart_write("minios> "); }

static void run(char *command) {
    while (*command == ' ') ++command;
    if (!*command) return;
    if (equals(command, "help")) {
        uart_write("commands: help, uptime, sched, mem, uart, echo <text>, pci, can [n], net, arp <ip>, sched rr|fp, rt [overload|reset]\n");
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
        bool first = true;
        for (uint32_t i = 1; i < scheduler_task_count(); ++i) {
            if (!scheduler_task_is_worker(i)) continue;
            if (!first) uart_write(",");
            first = false;
            uart_write_uint(scheduler_task_work(i));
        }
        uart_write(scheduler_policy() == SCHED_POLICY_FP ? " policy=fp\n" : " policy=rr\n");
    } else if (equals(command, "sched fp") || equals(command, "sched rr")) {
        scheduler_set_policy(command[6] == 'f' ? SCHED_POLICY_FP : SCHED_POLICY_RR);
        uart_write(scheduler_policy() == SCHED_POLICY_FP ? "sched: policy=fp rt stats reset\n"
                                                         : "sched: policy=rr rt stats reset\n");
    } else if (equals(command, "rt overload")) {
        rt_request_overload();
        uart_write("rt: next job of rt2 needs 25 ticks (deadline 20)\n");
    } else if (equals(command, "rt reset")) {
        rt_reset_stats();
        uart_write("rt: stats reset\n");
    } else if (equals(command, "rt")) {
        shell_rt_report();
    } else if (equals(command, "pci")) {
        char buf[80];
        for (unsigned i = 0; i < pci_device_count(); ++i) {
            const struct pci_device *d = pci_device_at(i);
            char *p = f_str(buf, "pci: ");
            pci_format(d, p);
            while (*p) ++p;
            p = f_str(p, " ");
            p = f_str(p, pci_class_name(d));
            *p++ = '\n'; *p = 0;
            uart_write(buf);
        }
    } else if (equals(command, "can") || starts_with(command, "can ")) {
        shell_can(command[3] ? command + 4 : "");
    } else if (equals(command, "net")) {
        shell_net();
    } else if (starts_with(command, "arp ")) {
        uint8_t ip[4];
        if (!net_parse_ip(command + 4, ip)) uart_write("arp: usage: arp a.b.c.d\n");
        else if (!net_send_arp_request(ip)) uart_write("arp: no network device\n");
        else uart_write("arp: who-has sent\n");
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
