#ifndef RT_H
#define RT_H
#include <stdbool.h>
#include <stdint.h>

/* Periodic real-time tasks with implicit deadlines (deadline = next release),
 * measured in 10 ms PIT ticks. Each job needs `exec` ticks of CPU, charged to
 * the task that is running when a tick arrives. A job still pending at its
 * deadline counts one deadline miss and is aborted. Timing in PIT counts
 * (1.193182 MHz, ~0.838 us) is read from the 8254 latch, so it reflects
 * QEMU's emulated PIT, not real hardware. */
#define RT_TASK_COUNT 3u

struct rt_report {
    uint8_t priority;
    uint32_t period_ticks, exec_ticks;
    uint32_t releases, completions, deadline_misses, max_response_ticks;
    uint32_t latency_samples, latency_min_pit, latency_max_pit;
};
struct rt_tick_report {
    uint32_t samples, min_pit, max_pit; /* interval between consecutive PIT IRQ0 entries */
};

bool rt_init(void);                                 /* creates the RT tasks; IRQs off */
void rt_tick(uint32_t ticks, uint32_t current_task); /* from the PIT ISR */
void rt_reset_stats(void);
void rt_request_overload(void);                     /* next job of the last task needs 25 ticks */
bool rt_report_for(unsigned index, struct rt_report *report);
struct rt_tick_report rt_tick_report(void);
uint32_t rt_pit_to_ns(uint32_t pit_counts);
#endif
