#include "scheduler.h"
#include "paging.h"
#include "frame.h"
#include "io.h"
#include "kprint.h"

#define STACK_PAGES 2u
#define STACK_BASE 0xC0000000u
struct task {
    struct interrupt_frame *frame;
    void (*entry)(void);
    volatile uint32_t work;
    volatile bool active;
};
static struct task tasks[SCHEDULER_MAX_TASKS];
static volatile uint32_t current;
static uint32_t created, switches;
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
    current = switches = 0;
    created = 1;
    tasks[0].active = true;
}
bool scheduler_create_task(void (*entry)(void)) {
    if (!entry || created >= SCHEDULER_MAX_TASKS) return false;
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
        return false;
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
    tasks[created].active = true;
    ++created;
    return true;
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
struct interrupt_frame *scheduler_tick(struct interrupt_frame *frame, uint32_t ticks) {
    if (ticks % SCHEDULER_QUANTUM_TICKS == 0 || !tasks[current].active) {
        tasks[current].frame = frame;
        uint32_t next = current;
        do { next = (next + 1) % created; } while (!tasks[next].active);
        if (next != current) {
            current = next;
            ++switches;
            frame = tasks[next].frame;
        }
    }
    vga_task(current, switches);
    if (ticks % 100 == 0) {
        debug_text("SCHED ticks=");
        debug_uint(ticks);
        debug_text(" switches=");
        debug_uint(switches);
        debug_text(" workers=");
        for (unsigned i = 1; i < created; ++i) {
            if (i != 1) outb(0xE9, ',');
            debug_uint(tasks[i].work);
        }
        outb(0xE9, '\n');
    }
    return frame;
}
