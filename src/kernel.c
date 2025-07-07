#include <stdint.h>
#include "keyboard.h"
#include "task.h"
extern void task_trampoline(void);

// IDT entry structure (x86)
struct idt_entry {
    uint16_t base_lo;
    uint16_t sel;
    uint8_t  always0;
    uint8_t  flags;
    uint16_t base_hi;
} __attribute__((packed));

// IDT pointer structure
struct idt_ptr {
    uint16_t limit;
    uint32_t base;
} __attribute__((packed));

#define IDT_SIZE 256
struct idt_entry idt[IDT_SIZE];
struct idt_ptr idtp;

extern void keyboard_handler(void);
extern void default_handler(void);
extern void exception_handler(void);
extern void asm_timer_on_interrupt(void);
extern void asm_page_fault_handler(void);
extern char stack_bottom, stack_top;

static inline void outb(uint16_t port, uint8_t val) {
    asm volatile ( "outb %0, %1" : : "a"(val), "Nd"(port) );
}

void idt_set_gate(int num, uint32_t base, uint16_t sel, uint8_t flags) {
    idt[num].base_lo = base & 0xFFFF;
    idt[num].base_hi = (base >> 16) & 0xFFFF;
    idt[num].sel = sel;
    idt[num].always0 = 0;
    idt[num].flags = flags;
}

void idt_load() {
    asm volatile("lidtl (%0)" : : "r" (&idtp));
}

void pic_remap() {
    // ICW1 - begin initialization
    outb(0x20, 0x11);
    outb(0xA0, 0x11);

    // ICW2 - remap offset address of IDT
    outb(0x21, 0x20); // Master PIC vector offset
    outb(0xA1, 0x28); // Slave PIC vector offset

    // ICW3 - setup cascading
    outb(0x21, 0x04);
    outb(0xA1, 0x02);

    // ICW4 - environment info
    outb(0x21, 0x01);
    outb(0xA1, 0x01);

    // Mask all except IRQ0 (timer) and IRQ1 (keyboard)
    outb(0x21, 0xFC); // 11111100: IRQ0 and IRQ1 enabled
    outb(0xA1, 0xFF); // all slave IRQs masked
}

// void keyboard_on_interrupt(uint8_t scancode) __attribute__((cdecl));
// void keyboard_on_interrupt(uint8_t scancode) {
//     volatile char *video = (volatile char*)0xB8000;
//     video[6] = 'A'; // Just write a fixed character for now
//     video[7] = 0x2E;
// }

// Add a local strcmp implementation for kernel use
int strcmp(const char *a, const char *b) {
    while (*a && (*a == *b)) {
        a++;
        b++;
    }
    return *(const unsigned char*)a - *(const unsigned char*)b;
}

// Add a local strlen implementation for kernel use
int strlen(const char *s) {
    int len = 0;
    while (s[len]) len++;
    return len;
}

// Add a local strncpy implementation for kernel use
char *strncpy(char *dest, const char *src, int n) {
    int i = 0;
    for (; i < n && src[i]; ++i)
        dest[i] = src[i];
    for (; i < n; ++i)
        dest[i] = 0;
    return dest;
}

// Make video memory pointer global for all functions
volatile char *video = (volatile char*)0xB8000;

void scroll_screen() {
    // Move all lines up by one
    for (int row = 1; row < 25; ++row) {
        for (int col = 0; col < 80; ++col) {
            video[((row - 1) * 80 + col) * 2] = video[(row * 80 + col) * 2];
            video[((row - 1) * 80 + col) * 2 + 1] = video[(row * 80 + col) * 2 + 1];
        }
    }
    // Clear the last line
    for (int col = 0; col < 80; ++col) {
        video[((24) * 80 + col) * 2] = ' ';
        video[((24) * 80 + col) * 2 + 1] = 0x0F;
    }
}

volatile int cursor_visible = 1;
volatile int cursor_blink_request = 0;

void timer_interrupt_handler(void) {
    static int tick = 0;
    tick++;
    task_tick(); // Update sleeping tasks
    if (tick % 25 == 0) { // Slower blink: about 2 blinks per second at 100Hz
        cursor_visible = !cursor_visible;
        cursor_blink_request = 1;
    }
    // task_yield(); // Removed for cooperative multitasking
}

// Free-list allocator for kernel heap (with alignment and safety checks)
#define KERNEL_HEAP_START 0x200000 // 2MB (moved up to avoid kernel overlap)
#define KERNEL_HEAP_SIZE  (128 * 1024) // 128KB heap for now
#define ALIGN8(x) (((x) + 7) & ~7)

typedef struct block_header {
    int size;
    int free;
    struct block_header *next;
} block_header_t;

static uint8_t *heap_base = (uint8_t*)KERNEL_HEAP_START;
static block_header_t *free_list = 0;

void heap_init() {
    // Zero the heap region
    for (int i = 0; i < KERNEL_HEAP_SIZE; ++i)
        heap_base[i] = 0;
    free_list = (block_header_t*)heap_base;
    free_list->size = KERNEL_HEAP_SIZE - sizeof(block_header_t);
    free_list->free = 1;
    free_list->next = 0;
}

void *kmalloc(int size) {
    size = ALIGN8(size);
    block_header_t *cur = free_list;
    while (cur) {
        if (cur->free && cur->size >= size) {
            if (cur->size >= size + (int)sizeof(block_header_t) + 8) { // Only split if enough space for a new block
                block_header_t *newblk = (block_header_t*)((uint8_t*)cur + sizeof(block_header_t) + size);
                newblk->size = cur->size - size - sizeof(block_header_t);
                newblk->free = 1;
                newblk->next = cur->next;
                cur->size = size;
                cur->next = newblk;
            }
            cur->free = 0;
            return (void*)((uint8_t*)cur + sizeof(block_header_t));
        }
        cur = cur->next;
    }
    return 0; // Out of memory
}

void kfree(void *ptr) {
    if (!ptr) return;
    block_header_t *blk = (block_header_t*)((uint8_t*)ptr - sizeof(block_header_t));
    blk->free = 1;
    // Coalesce adjacent free blocks
    block_header_t *cur = free_list;
    while (cur && cur->next) {
        if (cur->free && cur->next->free &&
            (uint8_t*)cur + sizeof(block_header_t) + cur->size == (uint8_t*)cur->next) {
            cur->size += sizeof(block_header_t) + cur->next->size;
            cur->next = cur->next->next;
        } else {
            cur = cur->next;
        }
    }
}

// Helper: convert an unsigned int to 8-digit hex string
void hex_to_str(unsigned int val, char *buf) {
    for (int i = 0; i < 8; ++i) {
        int nibble = (val >> (28 - 4 * i)) & 0xF;
        buf[i] = "0123456789ABCDEF"[nibble];
    }
    buf[8] = 0;
}

// Add prototype for print_line
void print_line(const char *str, int row);
void clear_screen();
void print_at(const char *str, int row, int col);

// --- Physical Memory Manager (PMM) ---
#define PMM_TOTAL_MEM (32 * 1024 * 1024) // 32MB for demo
#define PMM_PAGE_SIZE 4096
#define PMM_NUM_PAGES (PMM_TOTAL_MEM / PMM_PAGE_SIZE)
#define PMM_BITMAP_SIZE (PMM_NUM_PAGES / 8)
static uint8_t pmm_bitmap[PMM_BITMAP_SIZE];

void pmm_init() {
    for (int i = 0; i < PMM_BITMAP_SIZE; ++i) pmm_bitmap[i] = 0;
    // Mark pages used by kernel and heap as allocated
    int kernel_pages = KERNEL_HEAP_START / PMM_PAGE_SIZE;
    int heap_pages = (KERNEL_HEAP_START + KERNEL_HEAP_SIZE) / PMM_PAGE_SIZE;
    for (int i = 0; i < heap_pages; ++i) {
        pmm_bitmap[i / 8] |= (1 << (i % 8));
    }
}

void *alloc_page() {
    for (int i = 0; i < PMM_NUM_PAGES; ++i) {
        if (!(pmm_bitmap[i / 8] & (1 << (i % 8)))) {
            pmm_bitmap[i / 8] |= (1 << (i % 8));
            return (void *)(i * PMM_PAGE_SIZE);
        }
    }
    return 0; // Out of memory
}

void free_page(void *addr) {
    int i = ((uint32_t)addr) / PMM_PAGE_SIZE;
    pmm_bitmap[i / 8] &= ~(1 << (i % 8));
}

// --- Paging Structures ---
#define PAGE_PRESENT 0x1
#define PAGE_RW      0x2
#define PAGE_SIZE    4096
#define PAGE_ENTRIES 1024

// Page directory and one page table (identity map first 4MB)
__attribute__((aligned(4096))) static uint32_t page_directory[PAGE_ENTRIES];
__attribute__((aligned(4096))) static uint32_t first_page_table[PAGE_ENTRIES];

#define NUM_IDENTITY_TABLES 4
__attribute__((aligned(4096))) static uint32_t extra_page_tables[NUM_IDENTITY_TABLES-1][PAGE_ENTRIES];

void paging_init() {
    // Identity map first 16MB (4 page tables)
    for (int t = 0; t < NUM_IDENTITY_TABLES; ++t) {
        uint32_t *pt = (t == 0) ? first_page_table : extra_page_tables[t-1];
        for (int i = 0; i < PAGE_ENTRIES; ++i)
            pt[i] = ((t * PAGE_ENTRIES + i) * PAGE_SIZE) | PAGE_PRESENT | PAGE_RW;
        page_directory[t] = ((uint32_t)pt) | PAGE_PRESENT | PAGE_RW;
    }
    // Map the stack pages (in case stack is not within first 16MB, ensure mapped)
    uint32_t stack_start = (uint32_t)&stack_bottom;
    uint32_t stack_end = (uint32_t)&stack_top;
    for (uint32_t addr = stack_start & ~(PAGE_SIZE-1); addr < stack_end; addr += PAGE_SIZE) {
        int pd_idx = addr / (PAGE_ENTRIES * PAGE_SIZE);
        int pt_idx = (addr / PAGE_SIZE) % PAGE_ENTRIES;
        uint32_t *pt = (pd_idx == 0) ? first_page_table : extra_page_tables[pd_idx-1];
        pt[pt_idx] = (addr & 0xFFFFF000) | PAGE_PRESENT | PAGE_RW;
    }
    for (int i = NUM_IDENTITY_TABLES; i < PAGE_ENTRIES; ++i)
        page_directory[i] = 0;
    // Load page directory
    asm volatile ("mov %0, %%cr3" : : "r"(page_directory));
    // Enable paging
    uint32_t cr0;
    asm volatile ("mov %%cr0, %0" : "=r"(cr0));
    cr0 |= 0x80000000;
    asm volatile ("mov %0, %%cr0" : : "r"(cr0));
}

void page_fault_handler(uint32_t err_code) {
    uint32_t fault_addr;
    asm volatile ("mov %%cr2, %0" : "=r"(fault_addr));
    char buf[80], haddr[9];
    hex_to_str(fault_addr, haddr);
    int pos = 0;
    for (const char *s = "Page fault at "; *s; ++s) buf[pos++] = *s;
    for (int i = 0; i < 8; ++i) buf[pos++] = haddr[i];
    buf[pos++] = ' ';
    for (const char *s = "err: "; *s; ++s) buf[pos++] = *s;
    // Print error code as hex
    for (int i = 7; i >= 0; --i) buf[pos++] = "0123456789ABCDEF"[(err_code >> (i*4)) & 0xF];
    buf[pos] = 0;
    print_line(buf, 22);
    // Halt the system
    while (1) { asm volatile ("cli; hlt"); }
}

void shell_task(void) {
    print_line("SHELL START", 5);
    const char *msg = "Hello, World! Ku je ma bellushh@bella ma i qarti";
    int msg_len = 0;
    for (; msg[msg_len] != 0; ++msg_len) {
        video[msg_len * 2] = msg[msg_len];
        video[msg_len * 2 + 1] = 0x0F;
    }
    int input_pos = msg_len; // Start input after the message
    int max_input_pos = input_pos; // Track the furthest right the input has gone
    char cursor_saved_char = video[input_pos * 2];
    char cursor_saved_attr = video[input_pos * 2 + 1];
    // Draw initial block cursor (white background, black text)
    video[input_pos * 2 + 1] = 0x7F;
    
    unsigned int esp_val;
    asm volatile ("movl %%esp, %0" : "=r"(esp_val));
    video[160] = "0123456789ABCDEF"[(esp_val >> 28) & 0xF];
    video[161] = 0x2E;
    video[162] = "0123456789ABCDEF"[(esp_val >> 24) & 0xF];
    video[163] = 0x2E;
    video[164] = "0123456789ABCDEF"[(esp_val >> 20) & 0xF];
    video[165] = 0x2E;
    video[166] = "0123456789ABCDEF"[(esp_val >> 16) & 0xF];
    video[167] = 0x2E;
    video[168] = "0123456789ABCDEF"[(esp_val >> 12) & 0xF];
    video[169] = 0x2E;
    video[170] = "0123456789ABCDEF"[(esp_val >> 8) & 0xF];
    video[171] = 0x2E;
    video[172] = "0123456789ABCDEF"[(esp_val >> 4) & 0xF];
    video[173] = 0x2E;
    video[174] = "0123456789ABCDEF"[esp_val & 0xF];
    video[175] = 0x2E;
    
    unsigned short ds_val;
    asm volatile ("movw %%ds, %0" : "=r"(ds_val));
    video[180] = "0123456789ABCDEF"[(ds_val >> 12) & 0xF];
    video[181] = 0x2E;
    video[182] = "0123456789ABCDEF"[(ds_val >> 8) & 0xF];
    video[183] = 0x2E;
    video[184] = "0123456789ABCDEF"[(ds_val >> 4) & 0xF];
    video[185] = 0x2E;
    video[186] = "0123456789ABCDEF"[ds_val & 0xF];
    video[187] = 0x2E;
    
    keyboard_init();
    
    extern void keyboard_interrupt_handler(unsigned char);
    keyboard_interrupt_handler(0x1E); // Should write X or Y to screen if handler works
    
    //video[20] = '*'; // Should appear when a key is pressed
    video[21] = 0x4E;
    
    unsigned int handler_addr = (unsigned int)default_handler;
    video[200] = "0123456789ABCDEF"[(handler_addr >> 28) & 0xF];
    video[201] = 0x2E;
    video[202] = "0123456789ABCDEF"[(handler_addr >> 24) & 0xF];
    video[203] = 0x2E;
    video[204] = "0123456789ABCDEF"[(handler_addr >> 20) & 0xF];
    video[205] = 0x2E;
    video[206] = "0123456789ABCDEF"[(handler_addr >> 16) & 0xF];
    video[207] = 0x2E;
    video[208] = "0123456789ABCDEF"[(handler_addr >> 12) & 0xF];
    video[209] = 0x2E;
    video[210] = "0123456789ABCDEF"[(handler_addr >> 8) & 0xF];
    video[211] = 0x2E;
    video[212] = "0123456789ABCDEF"[(handler_addr >> 4) & 0xF];
    video[213] = 0x2E;
    video[214] = "0123456789ABCDEF"[handler_addr & 0xF];
    video[215] = 0x2E;
    
    #define LINE_LEN 80
    char input_line[LINE_LEN] = {0};
    int input_len = 0;
    int cursor_pos = 0; // Position in input_line (0..input_len)
    int input_screen_start = msg_len; // Where input starts on screen
    int screen_row = input_screen_start / 80; // Track current row
    const char *prompt = "amxos> ";
    int prompt_len = 7; // Length of the prompt string
    // Draw initial input line and block cursor
    for (int i = 0; i < LINE_LEN; ++i) {
        video[(input_screen_start + i) * 2] = ' ';
        video[(input_screen_start + i) * 2 + 1] = 0x0F;
    }
    // Draw prompt at start of input line
    for (int i = 0; i < prompt_len; ++i) {
        video[(input_screen_start + i) * 2] = prompt[i];
        video[(input_screen_start + i) * 2 + 1] = 0x0F;
    }
    video[(input_screen_start + prompt_len + cursor_pos) * 2 + 1] = 0x7F;

    #define HISTORY_SIZE 16
    char history[HISTORY_SIZE][LINE_LEN] = {{0}};
    int history_count = 0;
    int history_pos = 0; // For navigating history
    int browsing_history = 0; // 0: not browsing, 1: browsing

    // Program PIT for ~100Hz (blinking)
    outb(0x43, 0x36);
    outb(0x40, 0x9B);
    outb(0x40, 0x2E);
    
    while (1) {
        char c = keyboard_getchar();
        int blinked = 0;
        if (cursor_blink_request) {
            // Redraw cursor only
            int cur = input_screen_start + prompt_len + cursor_pos;
            if (cursor_visible)
                video[cur * 2 + 1] = 0x7F;
            else
                video[cur * 2 + 1] = 0x0F;
            cursor_blink_request = 0;
            blinked = 1;
        }
        if (c) {
            // Restore the character and attribute under the old cursor
            int cur = input_screen_start + prompt_len + cursor_pos;
            video[cur * 2] = input_line[cursor_pos] ? input_line[cursor_pos] : ' ';
            video[cur * 2 + 1] = 0x0F;

            if (c == '\b') { // Backspace
                if (cursor_pos > 0) {
                    for (int i = cursor_pos - 1; i < input_len - 1; ++i)
                        input_line[i] = input_line[i + 1];
                    input_line[input_len - 1] = 0;
                    cursor_pos--;
                    input_len--;
                }
                browsing_history = 0;
            } else if (c == '\n') { // Enter
                input_line[input_len] = 0; // Null-terminate
                if (input_len > 0) {
                    // Add to history if not duplicate of last
                    if (history_count == 0 || strcmp(input_line, history[(history_count - 1) % HISTORY_SIZE]) != 0) {
                        strncpy(history[history_count % HISTORY_SIZE], input_line, LINE_LEN);
                        history[history_count % HISTORY_SIZE][LINE_LEN - 1] = 0;
                        history_count++;
                    }
                    history_pos = history_count;
                    browsing_history = 0;
                    // Parse command and arguments
                    char *cmd = input_line;
                    while (*cmd == ' ') ++cmd; // skip leading spaces
                    char *args = cmd;
                    while (*args && *args != ' ') ++args;
                    if (*args) { *args = 0; ++args; } else { args = 0; }
                    if (!strcmp(cmd, "help")) {
                        print_line("Available commands: help, clear, echo, about, ls, memtest, pmmtest, pagingtest, faulttest", ++screen_row);
                    } else if (!strcmp(cmd, "clear")) {
                        clear_screen();
                        screen_row = 0;
                        input_screen_start = 0;
                    } else if (!strcmp(cmd, "echo")) {
                        if (args && *args) print_line(args, ++screen_row);
                        else print_line("", ++screen_row);
                    } else if (!strcmp(cmd, "about")) {
                        print_line("AMXOS: A simple x86 hobby OS shell", ++screen_row);
                    } else if (!strcmp(cmd, "ls")) {
                        print_line("help clear echo about ls memtest pmmtest pagingtest faulttest", ++screen_row);
                    } else if (!strcmp(cmd, "memtest")) {
                        void *a = kmalloc(32);
                        void *b = kmalloc(64);
                        void *c = kmalloc(16);
                        kfree(b);
                        void *d = kmalloc(48);
                        char buf[80];
                        char ha[9], hb[9], hc[9], hd[9];
                        hex_to_str((unsigned int)a, ha);
                        hex_to_str((unsigned int)b, hb);
                        hex_to_str((unsigned int)c, hc);
                        hex_to_str((unsigned int)d, hd);
                        int pos = 0;
                        for (const char *s = "kmalloc: "; *s; ++s) buf[pos++] = *s;
                        for (int i = 0; i < 8; ++i) buf[pos++] = ha[i];
                        buf[pos++] = ' ';
                        for (int i = 0; i < 8; ++i) buf[pos++] = hb[i];
                        buf[pos++] = ' ';
                        for (int i = 0; i < 8; ++i) buf[pos++] = hc[i];
                        buf[pos++] = ' ';
                        for (int i = 0; i < 8; ++i) buf[pos++] = hd[i];
                        buf[pos] = 0;
                        print_line(buf, ++screen_row);
                    } else if (!strcmp(cmd, "pmmtest")) {
                        void *p1 = alloc_page();
                        void *p2 = alloc_page();
                        void *p3 = alloc_page();
                        free_page(p2);
                        void *p4 = alloc_page();
                        char buf[80];
                        char h1[9], h2[9], h3[9], h4[9];
                        hex_to_str((unsigned int)p1, h1);
                        hex_to_str((unsigned int)p2, h2);
                        hex_to_str((unsigned int)p3, h3);
                        hex_to_str((unsigned int)p4, h4);
                        int pos = 0;
                        for (const char *s = "pages: "; *s; ++s) buf[pos++] = *s;
                        for (int i = 0; i < 8; ++i) buf[pos++] = h1[i];
                        buf[pos++] = ' ';
                        for (int i = 0; i < 8; ++i) buf[pos++] = h2[i];
                        buf[pos++] = ' ';
                        for (int i = 0; i < 8; ++i) buf[pos++] = h3[i];
                        buf[pos++] = ' ';
                        for (int i = 0; i < 8; ++i) buf[pos++] = h4[i];
                        buf[pos] = 0;
                        print_line(buf, ++screen_row);
                    } else if (!strcmp(cmd, "pagingtest")) {
                        print_line("Paging is enabled!", ++screen_row);
                    } else if (!strcmp(cmd, "faulttest")) {
                        volatile int *bad = (int*)0xDEADBEEF;
                        *bad = 42;
                    } else if (!strcmp(cmd, "testint21")) {
                        asm volatile("int $0x21");
                    } else if (!strcmp(cmd, "showidt0e")) {
                        char dbg[80], h[9];
                        hex_to_str(idt[0xE].base_lo | (idt[0xE].base_hi << 16), h);
                        for (int i = 0; i < 8; ++i) dbg[i] = h[i];
                        dbg[8] = 0;
                        print_line(dbg, ++screen_row);
                    } else if (*cmd) {
                        print_at("Unknown command: ", ++screen_row, 0);
                        print_at(cmd, screen_row, 18);
                    }
                } else {
                    ++screen_row;
                }
                for (int i = 0; i < input_len; ++i) input_line[i] = 0;
                for (int i = 0; i < LINE_LEN; ++i) {
                    video[((screen_row + 1) * 80 + i) * 2] = ' ';
                    video[((screen_row + 1) * 80 + i) * 2 + 1] = 0x0F;
                }
                // Always start prompt at the beginning of a new line
                screen_row++;
                input_len = 0;
                cursor_pos = 0;
                input_screen_start = screen_row * 80;
                // Draw prompt at start of new input line
                for (int i = 0; i < prompt_len; ++i) {
                    video[(input_screen_start + i) * 2] = prompt[i];
                    video[(input_screen_start + i) * 2 + 1] = 0x0F;
                }
            } else if (c == '\t') { // Tab
                int spaces = 4;
                int col = (input_screen_start + prompt_len + cursor_pos) % 80;
                int to_insert = (col + spaces > 80) ? (80 - col) : spaces;
                for (int s = 0; s < to_insert && input_len < LINE_LEN; ++s) {
                    for (int i = input_len; i > cursor_pos; --i)
                        input_line[i] = input_line[i - 1];
                    input_line[cursor_pos] = ' ';
                    cursor_pos++;
                    input_len++;
                }
                browsing_history = 0;
            } else if ((unsigned char)c == 0x80) { // Left arrow
                if (cursor_pos > 0) cursor_pos--;
                browsing_history = 0;
            } else if ((unsigned char)c == 0x81) { // Right arrow
                if (cursor_pos < input_len) cursor_pos++;
                browsing_history = 0;
            } else if ((unsigned char)c == 0x82) { // Up arrow
                if (history_count > 0 && history_pos > 0) {
                    if (!browsing_history) browsing_history = 1;
                    history_pos--;
                    strncpy(input_line, history[history_pos % HISTORY_SIZE], LINE_LEN);
                    input_line[LINE_LEN - 1] = 0;
                    input_len = cursor_pos = strlen(input_line);
                }
            } else if ((unsigned char)c == 0x83) { // Down arrow
                if (browsing_history && history_pos < history_count - 1) {
                    history_pos++;
                    strncpy(input_line, history[history_pos % HISTORY_SIZE], LINE_LEN);
                    input_line[LINE_LEN - 1] = 0;
                    input_len = cursor_pos = strlen(input_line);
                } else if (browsing_history && history_pos == history_count - 1) {
                    for (int i = 0; i < input_len; ++i) input_line[i] = 0;
                    input_len = cursor_pos = 0;
                    browsing_history = 0;
                }
            } else if ((unsigned char)c == 0x84) { // Home
                cursor_pos = 0;
                browsing_history = 0;
            } else if ((unsigned char)c == 0x85) { // End
                cursor_pos = input_len;
                browsing_history = 0;
            } else if ((unsigned char)c == 0x86) { // Delete
                if (cursor_pos < input_len) {
                    for (int i = cursor_pos; i < input_len - 1; ++i)
                        input_line[i] = input_line[i + 1];
                    input_line[input_len - 1] = 0;
                    input_len--;
                }
                browsing_history = 0;
            } else {
                if (input_len < LINE_LEN) {
                    for (int i = input_len; i > cursor_pos; --i)
                        input_line[i] = input_line[i - 1];
                    input_line[cursor_pos] = c;
                    cursor_pos++;
                    input_len++;
                }
                browsing_history = 0;
            }
            // Redraw input line (after prompt)
            for (int i = 0; i < LINE_LEN; ++i) {
                video[(input_screen_start + prompt_len + i) * 2] = input_line[i] ? input_line[i] : ' ';
                video[(input_screen_start + prompt_len + i) * 2 + 1] = 0x0F;
            }
            // Draw block cursor at new position (after prompt)
            int newcur = input_screen_start + prompt_len + cursor_pos;
            if (cursor_visible)
                video[newcur * 2 + 1] = 0x7F;
            else
                video[newcur * 2 + 1] = 0x0F;
        }
        task_yield(); // Yield to other tasks
    }
}

void idle_task(void) {
    while (1) { asm volatile ("hlt"); }
}

void test_sleep_task(void) {
    int row = 0;
    while (1) {
        print_line("=== TEST TASK RUNNING ===", row);
        task_sleep(100); // Sleep for 100 timer ticks (~1 second)
        print_line("=== TEST TASK WOKE UP  ===", row + 1);
        task_sleep(100);
        // Clear lines for next cycle
        print_line("                        ", row);
        print_line("                        ", row + 1);
        task_yield(); // Yield to other tasks
    }
}

void kmain(void) {
    pic_remap();
    heap_init();
    pmm_init();
    paging_init();
    // Register page fault handler (interrupt 0xE)
    idt_set_gate(0xE, (uint32_t)asm_page_fault_handler, 0x08, 0x8E);

    // Debug output: print handler address, IDT entry for 0xE, and ESP
    char dbgline[80], pfh[9], idtlo[9], idthi[9], idtsel[9], idtflg[9], espstr[9];
    hex_to_str((unsigned int)asm_page_fault_handler, pfh);
    hex_to_str((unsigned int)idt[0xE].base_lo, idtlo);
    hex_to_str((unsigned int)idt[0xE].base_hi, idthi);
    hex_to_str((unsigned int)idt[0xE].sel, idtsel);
    hex_to_str((unsigned int)idt[0xE].flags, idtflg);
    unsigned int esp_val;
    asm volatile ("movl %%esp, %0" : "=r"(esp_val));
    hex_to_str(esp_val, espstr);
    int dpos = 0;
    for (const char *s = "PFH:"; *s; ++s) dbgline[dpos++] = *s;
    for (int i = 0; i < 8; ++i) dbgline[dpos++] = pfh[i];
    dbgline[dpos++] = ' ';
    for (const char *s = "IDT0E:"; *s; ++s) dbgline[dpos++] = *s;
    for (int i = 0; i < 8; ++i) dbgline[dpos++] = idtlo[i];
    dbgline[dpos++] = ':';
    for (int i = 0; i < 8; ++i) dbgline[dpos++] = idthi[i];
    dbgline[dpos++] = ' ';
    for (const char *s = "SEL:"; *s; ++s) dbgline[dpos++] = *s;
    for (int i = 0; i < 8; ++i) dbgline[dpos++] = idtsel[i];
    dbgline[dpos++] = ' ';
    for (const char *s = "FLG:"; *s; ++s) dbgline[dpos++] = *s;
    for (int i = 0; i < 8; ++i) dbgline[dpos++] = idtflg[i];
    dbgline[dpos++] = ' ';
    for (const char *s = "ESP:"; *s; ++s) dbgline[dpos++] = *s;
    for (int i = 0; i < 8; ++i) dbgline[dpos++] = espstr[i];
    dbgline[dpos] = 0;
    print_line(dbgline, 2);

    // Set up IDT pointer
    idtp.limit = (sizeof(struct idt_entry) * IDT_SIZE) - 1;
    idtp.base = (uint32_t)&idt;

    // Print IDT pointer and handler address for debugging (after setup)
    char idtbase[9], idtlim[9], idtaddr[9];
    hex_to_str((unsigned int)idtp.base, idtbase);
    hex_to_str((unsigned int)idtp.limit, idtlim);
    hex_to_str((unsigned int)&idt, idtaddr);
    dpos = 0;
    for (const char *s = "idtp.base:"; *s; ++s) dbgline[dpos++] = *s;
    for (int i = 0; i < 8; ++i) dbgline[dpos++] = idtbase[i];
    dbgline[dpos++] = ' ';
    for (const char *s = "idtp.limit:"; *s; ++s) dbgline[dpos++] = *s;
    for (int i = 0; i < 8; ++i) dbgline[dpos++] = idtlim[i];
    dbgline[dpos++] = ' ';
    for (const char *s = "idt:"; *s; ++s) dbgline[dpos++] = *s;
    for (int i = 0; i < 8; ++i) dbgline[dpos++] = idtaddr[i];
    dbgline[dpos++] = ' ';
    for (const char *s = "pfh:"; *s; ++s) dbgline[dpos++] = *s;
    for (int i = 0; i < 8; ++i) dbgline[dpos++] = pfh[i];
    dbgline[dpos] = 0;
    print_line(dbgline, 5);

    // Set all entries to default_handler
    for (int i = 0; i < IDT_SIZE; i++) {
        idt_set_gate(i, (uint32_t)default_handler, 0x08, 0x8E);
    }
    // Set IRQ1 (keyboard) handler: vector 0x21
    extern void asm_keyboard_on_interrupt(void);
    idt_set_gate(0x21, (uint32_t)asm_keyboard_on_interrupt, 0x08, 0x8E);

    // Set IRQ0 (timer) handler: vector 0x20
    idt_set_gate(0x20, (uint32_t)asm_timer_on_interrupt, 0x08, 0x8E);

    // Set double fault handler: vector 0x8
    extern void asm_double_fault_handler(void);
    idt_set_gate(0x8, (uint32_t)asm_double_fault_handler, 0x08, 0x8E);

    // Load the IDT
    idt_load();

    asm volatile("sti");

    tasking_init();
    task_create(idle_task);
    task_create(shell_task);
    task_create(test_sleep_task);
    task_switch();
    // Should never reach here, but just in case
    while (1) { asm volatile ("hlt"); }
}

// Move these functions out of kmain and make them global functions
void print_line(const char *str, int row) {
    for (int i = 0; i < LINE_LEN && str[i]; ++i) {
        video[(row * 80 + i) * 2] = str[i];
        video[(row * 80 + i) * 2 + 1] = 0x0F;
    }
}
void clear_screen() {
    for (int i = 0; i < 25 * 80; ++i) {
        video[i * 2] = ' ';
        video[i * 2 + 1] = 0x0F;
    }
}
void print_at(const char *str, int row, int col) {
    for (int i = 0; str[i] && col + i < 80; ++i) {
        video[(row * 80 + col + i) * 2] = str[i];
        video[(row * 80 + col + i) * 2 + 1] = 0x0F;
    }
}