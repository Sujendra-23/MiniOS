#ifndef SCHEDULER_H
#define SCHEDULER_H
#include <stdbool.h>
#include <stdint.h>
#include "interrupts.h"

#define SCHEDULER_MAX_TASKS 4u
#define SCHEDULER_QUANTUM_TICKS 10u

/* Ring-0 tasks share the kernel address space. Initialize/create with IRQs off.
 * Each created task receives two mapped stack pages and an unmapped guard page.
 * The boot context occupies task 0; PIT IRQ0 preempts at 100 ms intervals. */
void scheduler_init(void);
bool scheduler_create_task(void (*entry)(void));
struct interrupt_frame *scheduler_tick(struct interrupt_frame *frame, uint32_t ticks);
void scheduler_task_entry(void);
void scheduler_record_work(void);
uint32_t scheduler_current_task(void);
#endif
