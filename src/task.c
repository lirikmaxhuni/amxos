#include "task.h"
#include <stddef.h>

#define MAX_TASKS 8
#define STACK_SIZE 4096

static task_t tasks[MAX_TASKS];
static int num_tasks = 0;
static task_t *current_task = NULL;

// Simple round-robin linked list
static task_t *task_list_head = NULL;

// Forward declaration for context switch (to be implemented in assembly)
extern void context_switch(cpu_context_t *old, cpu_context_t *new);
extern void task_exit(void); // Use as fake return address
extern void print_line(const char*, int); // For debug prints
extern void task_trampoline(void); // Trampoline for task startup

void print_hex(uint32_t val, int row) {
    char buf[16];
    for (int i = 0; i < 16; ++i) buf[i] = 0;
    for (int i = 7; i >= 0; --i) {
        int nib = (val >> (i*4)) & 0xF;
        buf[7-i] = "0123456789ABCDEF"[nib];
    }
    print_line(buf, row);
}

__attribute__((weak)) void context_switch(cpu_context_t *old, cpu_context_t *new) {
    // Stub: real context switch will be implemented in assembly
}

void tasking_init(void) {
    num_tasks = 0;
    task_list_head = NULL;
    current_task = NULL;
}

task_t *task_create(void (*entry)(void)) {
    if (num_tasks >= MAX_TASKS) return NULL;
    task_t *t = &tasks[num_tasks++];
    t->id = num_tasks;
    t->state = TASK_READY;
    t->stack = (uint32_t*)kmalloc(STACK_SIZE);
    if (!t->stack) return NULL;
    // Set up initial stack for trampoline: [task_exit][entry]
    uint32_t *stack_top = t->stack + STACK_SIZE/sizeof(uint32_t);
    *--stack_top = (uint32_t)task_exit;     // Fake return address
    *--stack_top = (uint32_t)entry;         // Entry function pointer
    t->context.eip = (uint32_t)task_trampoline;
    t->context.esp = (uint32_t)stack_top;
    t->context.ebp = (uint32_t)stack_top;
    t->context.edi = t->context.esi = t->context.ebx = 0;
    t->next = NULL;
    t->sleep_ticks = 0;
    // Add to task list
    if (!task_list_head) {
        task_list_head = t;
    } else {
        task_t *cur = task_list_head;
        while (cur->next) cur = cur->next;
        cur->next = t;
    }
    if (!current_task) current_task = t;
    // Debug print
    char msg[32];
    for (int i = 0; i < 32; ++i) msg[i] = 0;
    msg[0] = 'C'; msg[1] = 'r'; msg[2] = 'e'; msg[3] = 'a'; msg[4] = 't'; msg[5] = 'e'; msg[6] = 'd'; msg[7] = ' '; msg[8] = 'T'; msg[9] = 'a'; msg[10] = 's'; msg[11] = 'k'; msg[12] = ' '; msg[13] = '#';
    msg[14] = '0' + t->id;
    print_line(msg, t->id + 2);
    // Print trampoline address, entry, esp, and stack contents
    print_line("Tramp:", 10); print_hex((uint32_t)task_trampoline, 11);
    print_line("Entry:", 12); print_hex((uint32_t)entry, 13);
    print_line("ESP:", 14); print_hex((uint32_t)stack_top, 15);
    print_line("Stack0:", 16); print_hex(stack_top[0], 17);
    print_line("Stack1:", 18); print_hex(stack_top[1], 19);
    return t;
}

// Helper for debug: print all task states
static void debug_print_all_tasks(void) {
    task_t *t = task_list_head;
    int row = 22;
    char msg[32];
    for (int i = 0; i < 32; ++i) msg[i] = 0;
    int col = 0;
    while (t) {
        // Format: ID:STATE  (e.g., 1:R 2:S 3:T)
        msg[col++] = '0' + t->id;
        msg[col++] = ':';
        switch (t->state) {
            case TASK_RUNNING:   msg[col++] = 'R'; break;
            case TASK_READY:     msg[col++] = 'D'; break;
            case TASK_BLOCKED:   msg[col++] = 'B'; break;
            case TASK_SLEEPING:  msg[col++] = 'S'; break;
            case TASK_TERMINATED:msg[col++] = 'T'; break;
            default:             msg[col++] = '?'; break;
        }
        msg[col++] = ' ';
        t = t->next;
    }
    print_line(msg, row);
}

// Modular scheduler: round-robin for now, skip sleeping/blocked/terminated
static task_t* schedule(void) {
    debug_print_all_tasks(); // Print all task states each time scheduler runs
    if (!current_task) return task_list_head;
    task_t *start = current_task;
    task_t *next = current_task->next ? current_task->next : task_list_head;
    while (next != start) {
        if (next->state == TASK_READY) {
            // Debug print
            char msg[32];
            for (int i = 0; i < 32; ++i) msg[i] = 0;
            msg[0] = 'S'; msg[1] = 'c'; msg[2] = 'h'; msg[3] = 'e'; msg[4] = 'd'; msg[5] = 'u'; msg[6] = 'l'; msg[7] = 'e'; msg[8] = ' '; msg[9] = 'T'; msg[10] = 'a'; msg[11] = 's'; msg[12] = 'k'; msg[13] = '#';
            msg[14] = '0' + next->id;
            print_line(msg, 20); // Print to row 20
            return next;
        }
        next = next->next ? next->next : task_list_head;
    }
    if (current_task->state == TASK_READY) {
        char msg[32];
        for (int i = 0; i < 32; ++i) msg[i] = 0;
        msg[0] = 'S'; msg[1] = 'c'; msg[2] = 'h'; msg[3] = 'e'; msg[4] = 'd'; msg[5] = 'u'; msg[6] = 'l'; msg[7] = 'e'; msg[8] = ' '; msg[9] = 'T'; msg[10] = 'a'; msg[11] = 's'; msg[12] = 'k'; msg[13] = '#';
        msg[14] = '0' + current_task->id;
        print_line(msg, 20);
        return current_task;
    }
    next = task_list_head;
    do {
        if (next->state == TASK_READY) {
            char msg[32];
            for (int i = 0; i < 32; ++i) msg[i] = 0;
            msg[0] = 'S'; msg[1] = 'c'; msg[2] = 'h'; msg[3] = 'e'; msg[4] = 'd'; msg[5] = 'u'; msg[6] = 'l'; msg[7] = 'e'; msg[8] = ' '; msg[9] = 'T'; msg[10] = 'a'; msg[11] = 's'; msg[12] = 'k'; msg[13] = '#';
            msg[14] = '0' + next->id;
            print_line(msg, 20);
            return next;
        }
        next = next->next ? next->next : task_list_head;
    } while (next != task_list_head);
    return current_task;
}

void task_switch(void) {
    if (!current_task) return;
    task_t *prev = current_task;
    task_t *next = schedule();
    if (next == prev) return; // Only one runnable task
    current_task = next;
    context_switch(&prev->context, &next->context);
}

void task_yield(void) {
    task_switch();
}

void task_exit(void) {
    if (!current_task) return;
    print_line("TASK EXIT", 6); // Debug print
    current_task->state = TASK_TERMINATED;
    task_switch();
}

task_t *get_current_task(void) {
    return current_task;
}

// --- Sleeping/Blocking Support ---
void task_sleep(int ticks) {
    if (!current_task || ticks <= 0) return;
    current_task->sleep_ticks = ticks;
    current_task->state = TASK_SLEEPING;
    task_switch();
}

void task_wake(task_t *t) {
    if (!t) return;
    t->sleep_ticks = 0;
    if (t->state == TASK_SLEEPING || t->state == TASK_BLOCKED)
        t->state = TASK_READY;
}

// Call this from the timer interrupt handler to update sleeping tasks
task_t *task_list(void) { return task_list_head; }
void task_tick(void) {
    task_t *t = task_list_head;
    while (t) {
        if (t->state == TASK_SLEEPING && t->sleep_ticks > 0) {
            t->sleep_ticks--;
            if (t->sleep_ticks == 0) {
                t->state = TASK_READY;
            }
        }
        t = t->next;
    }
} 