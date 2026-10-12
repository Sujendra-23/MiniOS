#ifndef SCHEDULER_H
#define SCHEDULER_H
#include <stdbool.h>
#include <stdint.h>
#include "interrupts.h"

#define SCHEDULER_MAX_TASKS 8u
#define SCHEDULER_QUANTUM_TICKS 10u
/* Lower number = higher priority. Task 0 (shell) and the CPU-bound workers
 * share the background level; real-time tasks use levels 1..9. */
#define SCHED_PRIO_BACKGROUND 10u

enum sched_policy {
    SCHED_POLICY_RR = 0, /* round-robin over all ready tasks, 100 ms quantum */
    SCHED_POLICY_FP = 1  /* fixed priority, preemptive each tick; round-robin within a level */
};

/* Ring-0 tasks share the kernel address space. Initialize/create with IRQs off.
 * Each created task receives two mapped stack pages and an unmapped guard page.
 * The boot context occupies task 0; PIT IRQ0 preempts at 10 ms ticks. */
void scheduler_init(void);
/* Returns the new task id, or -1. counts_work marks a CPU-bound worker. */
int scheduler_spawn(void (*entry)(void), uint8_t priority, bool counts_work);
bool scheduler_create_task(void (*entry)(void));
struct interrupt_frame *scheduler_tick(struct interrupt_frame *frame, uint32_t ticks);
void scheduler_task_entry(void);
void scheduler_record_work(void);
uint32_t scheduler_current_task(void);
uint32_t scheduler_switches(void);
uint32_t scheduler_task_count(void);
uint32_t scheduler_task_work(uint32_t task);
bool scheduler_task_is_worker(uint32_t task);
void scheduler_set_policy(enum sched_policy policy);
enum sched_policy scheduler_policy(void);
/* A blocked task is skipped until unblocked (the RT module does both from the tick). */
void scheduler_set_blocked(uint32_t task, bool blocked);
#endif
