#ifndef KERNEL_H
#define KERNEL_H

#include <stdint.h>

// IDT and interrupt setup
void idt_set_gate(int num, uint32_t base, uint16_t sel, uint8_t flags);
void idt_load(void);
void pic_remap(void);

// Preemption control
void preempt_disable_enter(void);
void preempt_disable_exit(void);

// Timer interrupt
void timer_interrupt_handler(void);

// Heap allocator
void heap_init(void);
void *kmalloc(int size);
void kfree(void *ptr);

// Physical memory manager
void pmm_init(void);
void *alloc_page(void);
void free_page(void *addr);

// Paging
void paging_init(void);

// Panic
void kernel_panic(const char *msg);

// Screen/console
void print_line(const char *str, int row);
void clear_screen(void);
void print_at(const char *str, int row, int col);
void scroll_screen(void);

// Utility
void hex_to_str(unsigned int val, char *buf);

// Task entry points
void shell_task(void);
void idle_task(void);
void test_sleep_task(void);

// Main kernel entry
void kmain(void);

#endif // KERNEL_H 