#ifndef TASK_H
#define TASK_H

#include <stdint.h>

// Task states
typedef enum {
    TASK_RUNNING,
    TASK_READY,
    TASK_BLOCKED,
    TASK_SLEEPING,
    TASK_TERMINATED
} task_state_t;

// CPU context (registers to save/restore)
typedef struct cpu_context {
    uint32_t edi, esi, ebx, ebp, esp, eip;
} cpu_context_t;

// Task structure
typedef struct task {
    cpu_context_t context;
    uint32_t *stack;
    int id;
    task_state_t state;
    struct task *next;
    int sleep_ticks; // Number of timer ticks left to sleep
} task_t;

void tasking_init(void);
task_t *task_create(void (*entry)(void));
void task_switch(void);
void task_yield(void);
void task_exit(void);
void task_sleep(int ticks); // Sleep for a number of timer ticks
void task_wake(task_t *t); // Wake a sleeping or blocked task

task_t *get_current_task(void);

#endif // TASK_H 