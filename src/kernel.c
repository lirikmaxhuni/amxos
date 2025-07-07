#include <stdint.h>

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

    // Mask all except IRQ1 (keyboard)
    outb(0x21, 0xFD); // 11111101: only IRQ1 enabled
    outb(0xA1, 0xFF); // all slave IRQs masked
}

// void keyboard_on_interrupt(uint8_t scancode) __attribute__((cdecl));
// void keyboard_on_interrupt(uint8_t scancode) {
//     volatile char *video = (volatile char*)0xB8000;
//     video[6] = 'A'; // Just write a fixed character for now
//     video[7] = 0x2E;
// }

void kmain(void) {
    pic_remap();

    // Set up IDT pointer
    idtp.limit = (sizeof(struct idt_entry) * IDT_SIZE) - 1;
    idtp.base = (uint32_t)&idt;

    // Zero out the IDT
    for (int i = 0; i < 32; i++) {
        idt_set_gate(i, (uint32_t)exception_handler, 0x08, 0x8E);
    }
    for (int i = 32; i < IDT_SIZE; i++) {
        idt_set_gate(i, (uint32_t)default_handler, 0x08, 0x8E);
    }

    // Set IRQ1 (keyboard) handler: vector 0x21
    idt_set_gate(0x21, (uint32_t)keyboard_handler, 0x08, 0x8E);

    // Load the IDT
    idt_load();

    asm volatile("sti");

    volatile char *video = (volatile char*)0xB8000;
    const char *msg = "Hello, World! Ku je ma bellushh@bella ma i qarti";
    for (int i = 0; msg[i] != 0; ++i) {
        video[i * 2] = msg[i];
        video[i * 2 + 1] = 0x0F;
    }
    
    video[2] = 'Y';
    //video[3] = 0x2F;
    
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
    
    while (1) {}
}