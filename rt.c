#include "rt.h"
#include "scheduler.h"
#include "interrupts.h"
#include "io.h"

#define OVERLOAD_EXEC_TICKS 25u

struct rt_task {
    int task;
    uint8_t priority;
    uint32_t period, exec;
    volatile bool pending;
    volatile bool started;
    volatile uint32_t remaining, deadline, release_tick, release_pit;
    struct rt_report stats;
};
static struct rt_task rt[RT_TASK_COUNT] = {
    {-1, 1, 5, 1, false, false, 0, 0, 0, 0, {0}},   /* 50 ms period, 10 ms job */
    {-1, 2, 10, 2, false, false, 0, 0, 0, 0, {0}},  /* 100 ms period, 20 ms job */
    {-1, 3, 20, 3, false, false, 0, 0, 0, 0, {0}},  /* 200 ms period, 30 ms job */
};
static volatile bool overload_next;
static struct rt_tick_report tick_stats;
static uint32_t last_pit;
static bool have_last;

/* Fine-grained time in PIT counts: whole ticks plus elapsed count of the
 * current period (mode 2 counts down from the divisor). Call with IRQs off. */
static uint32_t pit_now(uint32_t ticks) {
    outb(0x43, 0x00); /* latch channel 0 */
    uint32_t count = inb(0x40);
    count |= (uint32_t)inb(0x40) << 8;
    if (count == 0 || count > PIT_DIVISOR) count = PIT_DIVISOR;
    return ticks * PIT_DIVISOR + (PIT_DIVISOR - count);
}
uint32_t rt_pit_to_ns(uint32_t pit_counts) {
    return pit_counts > 4000000u ? 3352380000u : pit_counts * 838u; /* 838.1 ns per count */
}

static struct rt_task *mine(void) {
    int id = (int)scheduler_current_task();
    for (unsigned i = 0; i < RT_TASK_COUNT; ++i)
        if (rt[i].task == id) return &rt[i];
    return &rt[0];
}
/* Sleep until a job is pending; record how long after its release this
 * task first got the CPU. Returns the release tick of the job to run. */
static uint32_t wait_for_job(struct rt_task *t) {
    for (;;) {
        __asm__ volatile ("cli" : : : "memory");
        if (t->pending) {
            if (!t->started) {
                t->started = true;
                uint32_t latency = pit_now(interrupts_ticks()) - t->release_pit;
                struct rt_report *s = &t->stats;
                if (!s->latency_samples || latency < s->latency_min_pit) s->latency_min_pit = latency;
                if (latency > s->latency_max_pit) s->latency_max_pit = latency;
                ++s->latency_samples;
            }
            uint32_t release = t->release_tick;
            __asm__ volatile ("sti" : : : "memory");
            return release;
        }
        scheduler_set_blocked((uint32_t)t->task, true);
        __asm__ volatile ("sti; hlt" : : : "memory"); /* the tick switches us out */
    }
}
static void rt_task_entry(void) {
    struct rt_task *t = mine();
    for (;;) {
        uint32_t release = wait_for_job(t);
        /* The job is "executed" by holding the CPU: the tick ISR charges one
         * unit per tick this task is running, and ends the job at zero. */
        while (t->pending && t->release_tick == release) __asm__ volatile ("pause");
    }
}
bool rt_init(void) {
    for (unsigned i = 0; i < RT_TASK_COUNT; ++i) {
        rt[i].task = scheduler_spawn(rt_task_entry, rt[i].priority, false);
        if (rt[i].task < 0) return false;
        scheduler_set_blocked((uint32_t)rt[i].task, true); /* until its first release */
        rt[i].stats.priority = rt[i].priority;
        rt[i].stats.period_ticks = rt[i].period;
        rt[i].stats.exec_ticks = rt[i].exec;
    }
    return true;
}
void rt_reset_stats(void) {
    uint32_t flags;
    __asm__ volatile ("pushf; pop %0; cli" : "=r"(flags) : : "memory");
    for (unsigned i = 0; i < RT_TASK_COUNT; ++i) {
        struct rt_report *s = &rt[i].stats;
        s->releases = s->completions = s->deadline_misses = s->max_response_ticks = 0;
        s->latency_samples = s->latency_min_pit = s->latency_max_pit = 0;
        /* Drop in-flight jobs so the new policy is measured from a clean start. */
        if (rt[i].task >= 0 && rt[i].pending) {
            rt[i].pending = false;
            scheduler_set_blocked((uint32_t)rt[i].task, true);
        }
    }
    tick_stats.samples = tick_stats.min_pit = tick_stats.max_pit = 0;
    have_last = false;
    if (flags & 0x200) __asm__ volatile ("sti" : : : "memory");
}
void rt_request_overload(void) { overload_next = true; }

void rt_tick(uint32_t ticks, uint32_t current_task) {
    uint32_t now = pit_now(ticks);
    if (have_last) {
        uint32_t delta = now - last_pit;
        if (!tick_stats.samples || delta < tick_stats.min_pit) tick_stats.min_pit = delta;
        if (delta > tick_stats.max_pit) tick_stats.max_pit = delta;
        ++tick_stats.samples;
    }
    last_pit = now;
    have_last = true;
    for (unsigned i = 0; i < RT_TASK_COUNT; ++i) {
        struct rt_task *t = &rt[i];
        if (t->task < 0) continue;
        /* 1. Charge the tick that just elapsed to the running job. */
        if (t->pending && (uint32_t)t->task == current_task && t->remaining && --t->remaining == 0) {
            t->pending = false;
            ++t->stats.completions;
            uint32_t response = ticks - t->release_tick;
            if (response > t->stats.max_response_ticks) t->stats.max_response_ticks = response;
            scheduler_set_blocked((uint32_t)t->task, true);
        }
        /* 2. Still pending at the deadline: miss, abort the job. */
        if (t->pending && ticks >= t->deadline) {
            ++t->stats.deadline_misses;
            t->pending = false;
            scheduler_set_blocked((uint32_t)t->task, true);
        }
        /* 3. Periodic release. */
        if (ticks % t->period == 0) {
            ++t->stats.releases;
            t->remaining = t->exec;
            if (i == RT_TASK_COUNT - 1 && overload_next) {
                t->remaining = OVERLOAD_EXEC_TICKS;
                overload_next = false;
            }
            t->release_tick = ticks;
            t->deadline = ticks + t->period;
            t->release_pit = now;
            t->started = false;
            t->pending = true;
            scheduler_set_blocked((uint32_t)t->task, false);
        }
    }
}
bool rt_report_for(unsigned index, struct rt_report *report) {
    if (index >= RT_TASK_COUNT) return false;
    *report = rt[index].stats;
    return true;
}
struct rt_tick_report rt_tick_report(void) { return tick_stats; }
