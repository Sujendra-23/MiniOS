#include "scheduler.h"
#include "paging.h"
#include "frame.h"
#include "io.h"
#include "kprint.h"
#include "rt.h"

#define STACK_PAGES 2u
#define STACK_BASE 0xC0000000u
struct task {
    struct interrupt_frame *frame;
    void (*entry)(void);
    volatile uint32_t work;
    volatile bool active;
    volatile bool blocked;
    bool counts_work;
    uint8_t priority;
};
static struct task tasks[SCHEDULER_MAX_TASKS];
static volatile uint32_t current;
static uint32_t created, switches, bg_cursor;
static volatile enum sched_policy policy;
extern void scheduler_task_start(void);

static void debug_text(const char *s) {
    while (*s) outb(0xE9, *s++);
}
static void debug_uint(uint32_t value) {
    char digits[10];
    unsigned n = 0;
    do { digits[n++] = '0' + value % 10; value /= 10; } while (value);
    while (n) outb(0xE9, digits[--n]);
}
void scheduler_init(void) {
    current = switches = bg_cursor = 0;
    created = 1;
    policy = SCHED_POLICY_RR;
    tasks[0].active = true;
    tasks[0].priority = SCHED_PRIO_BACKGROUND;
}
int scheduler_spawn(void (*entry)(void), uint8_t priority, bool counts_work) {
    if (!entry || created >= SCHEDULER_MAX_TASKS) return -1;
    uint32_t bottom = STACK_BASE + created * (STACK_PAGES + 1) * FRAME_SIZE + FRAME_SIZE;
    unsigned mapped = 0;
    for (; mapped < STACK_PAGES; ++mapped) {
        uint32_t physical = frame_alloc();
        if (!physical) break;
        if (!paging_map(bottom + mapped * FRAME_SIZE, physical, true)) {
            frame_free(physical);
            break;
        }
    }
    if (mapped != STACK_PAGES) {
        while (mapped) frame_free(paging_unmap(bottom + --mapped * FRAME_SIZE));
        return -1;
    }
    uint32_t top = bottom + STACK_PAGES * FRAME_SIZE;
    struct interrupt_frame *frame = (struct interrupt_frame *)(top - sizeof(*frame));
    for (unsigned i = 0; i < sizeof(*frame) / sizeof(uint32_t); ++i)
        ((uint32_t *)frame)[i] = 0;
    frame->eip = (uint32_t)scheduler_task_start;
    frame->cs = 0x08;
    frame->eflags = 0x202; /* Interrupts enabled after the first IRET. */
    tasks[created].frame = frame;
    tasks[created].entry = entry;
    tasks[created].work = 0;
    tasks[created].blocked = false;
    tasks[created].counts_work = counts_work;
    tasks[created].priority = priority;
    tasks[created].active = true;
    return (int)created++;
}
bool scheduler_create_task(void (*entry)(void)) {
    return scheduler_spawn(entry, SCHED_PRIO_BACKGROUND, true) >= 0;
}
bool scheduler_task_is_worker(uint32_t task) { return task < created && tasks[task].counts_work; }
enum sched_policy scheduler_policy(void) { return policy; }
void scheduler_set_blocked(uint32_t task, bool blocked) {
    if (task < created) tasks[task].blocked = blocked;
}
void scheduler_set_policy(enum sched_policy next) {
    uint32_t flags;
    __asm__ volatile ("pushf; pop %0; cli" : "=r"(flags) : : "memory");
    policy = next;
    bg_cursor = tasks[current].priority == SCHED_PRIO_BACKGROUND ? current : 0;
    rt_reset_stats();
    if (flags & 0x200) __asm__ volatile ("sti" : : : "memory");
}
uint32_t scheduler_current_task(void) { return current; }
uint32_t scheduler_switches(void) { return switches; }
uint32_t scheduler_task_count(void) { return created; }
uint32_t scheduler_task_work(uint32_t task) { return task < created ? tasks[task].work : 0; }
void scheduler_record_work(void) { ++tasks[current].work; }
void scheduler_task_entry(void) {
    tasks[current].entry();
    /* Returning tasks stop running. Their stacks remain reserved until reboot. */
    __asm__ volatile ("cli" : : : "memory");
    tasks[current].active = false;
    for (;;) __asm__ volatile ("sti; hlt" : : : "memory");
}
static bool ready(uint32_t task) { return tasks[task].active && !tasks[task].blocked; }
static uint32_t next_ready_after(uint32_t from) {
    for (uint32_t k = 1; k <= created; ++k) {
        uint32_t i = (from + k) % created;
        if (ready(i)) return i;
    }
    return from;
}
/* Highest-priority ready task; background tasks rotate through bg_cursor. */
static uint32_t pick_fixed_priority(void) {
    uint32_t best = created;
    for (uint32_t i = 0; i < created; ++i)
        if (ready(i) && (best == created || tasks[i].priority < tasks[best].priority))
            best = i;
    if (best == created) return current;
    if (tasks[best].priority == SCHED_PRIO_BACKGROUND && ready(bg_cursor)) return bg_cursor;
    return best;
}
static void advance_bg_cursor(void) {
    for (uint32_t k = 1; k <= created; ++k) {
        uint32_t i = (bg_cursor + k) % created;
        if (ready(i) && tasks[i].priority == SCHED_PRIO_BACKGROUND) { bg_cursor = i; return; }
    }
}
struct interrupt_frame *scheduler_tick(struct interrupt_frame *frame, uint32_t ticks) {
    rt_tick(ticks, current); /* charge, deadline checks, releases (may unblock tasks) */
    uint32_t next = current;
    if (policy == SCHED_POLICY_FP) {
        if (ticks % SCHEDULER_QUANTUM_TICKS == 0) advance_bg_cursor();
        next = pick_fixed_priority();
    } else if (ticks % SCHEDULER_QUANTUM_TICKS == 0 || !ready(current)) {
        next = next_ready_after(current);
    }
    if (next != current) {
        tasks[current].frame = frame;
        current = next;
        ++switches;
        frame = tasks[next].frame;
    }
    vga_task(current, switches);
    if (ticks % 100 == 0) {
        debug_text("SCHED ticks=");
        debug_uint(ticks);
        debug_text(" switches=");
        debug_uint(switches);
        debug_text(" workers=");
        bool first = true;
        for (unsigned i = 1; i < created; ++i) {
            if (!tasks[i].counts_work) continue;
            if (!first) outb(0xE9, ',');
            first = false;
            debug_uint(tasks[i].work);
        }
        outb(0xE9, '\n');
    }
    return frame;
}
