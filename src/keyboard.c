#include "keyboard.h"
#include <stdint.h>
#include "debug.h"

#define KB_BUFFER_SIZE 128

static char kb_buffer[KB_BUFFER_SIZE];
static uint8_t kb_head = 0;
static uint8_t kb_tail = 0;

// Simple US QWERTY scancode to ASCII table (partial, for demonstration)
static const char scancode_ascii[128] = {
    0,  27, '1','2','3','4','5','6','7','8','9','0','-','=', '\b', // 0x0E: Backspace
    '\t','q','w','e','r','t','y','u','i','o','p','[',']','\n', // 0x1C: Enter
    0,  'a','s','d','f','g','h','j','k','l',';','\'','`', 0,  // 0x2A: LShift
    '\\','z','x','c','v','b','n','m',',','.','/', 0, '*', 0,  ' ', // 0x39: Space
    // ... (fill out as needed)
};

// Shifted ASCII table for when Shift is held
static const char scancode_ascii_shift[128] = {
    0,  27, '!', '@', '#', '$', '%', '^', '&', '*', '(', ')', '_', '+', '\b',
    '\t','Q','W','E','R','T','Y','U','I','O','P','{','}','\n',
    0,  'A','S','D','F','G','H','J','K','L',':','"','~', 0,
    '|','Z','X','C','V','B','N','M','<','>','?', 0, '*', 0,  ' ',
    // ... (fill out as needed)
};

static uint8_t shift_pressed = 0;
static uint8_t e0_prefix = 0; // Track if 0xE0 prefix was received

void keyboard_interrupt_handler(uint8_t scancode) {
    // Handle Shift press/release
    if (scancode == 0x2A || scancode == 0x36) { // Left or Right Shift pressed
        shift_pressed = 1;
        return;
    } else if (scancode == 0xAA || scancode == 0xB6) { // Left or Right Shift released
        shift_pressed = 0;
        return;
    }
    // Handle extended (0xE0) prefix for arrow keys
    if (scancode == 0xE0) {
        e0_prefix = 1;
        return;
    }
    if (e0_prefix) {
        // Left arrow: 0x4B, Right arrow: 0x4D, Up arrow: 0x48, Down arrow: 0x50, Home: 0x47, End: 0x4F, Delete: 0x53
        uint8_t special = 0;
        if (scancode == 0x4B) special = 0x80; // Left arrow
        else if (scancode == 0x4D) special = 0x81; // Right arrow
        else if (scancode == 0x48) special = 0x82; // Up arrow
        else if (scancode == 0x50) special = 0x83; // Down arrow
        else if (scancode == 0x47) special = 0x84; // Home
        else if (scancode == 0x4F) special = 0x85; // End
        else if (scancode == 0x53) special = 0x86; // Delete
        e0_prefix = 0;
        if (special) {
            uint8_t next_head = (kb_head + 1) % KB_BUFFER_SIZE;
            if (next_head != kb_tail) {
                kb_buffer[kb_head] = special;
                kb_head = next_head;
            }
        }
        return;
    }
    // Only handle key press (ignore key release)
    if (scancode < 128) {
        char c = shift_pressed ? scancode_ascii_shift[scancode] : scancode_ascii[scancode];
        if (c) {
            uint8_t next_head = (kb_head + 1) % KB_BUFFER_SIZE;
            if (next_head != kb_tail) { // buffer not full
                kb_buffer[kb_head] = c;
                kb_head = next_head;
            }
        }
    }
}

char keyboard_getchar(void) {
    if (kb_head == kb_tail)
        return 0; // Buffer empty
    char c = kb_buffer[kb_tail];
    kb_tail = (kb_tail + 1) % KB_BUFFER_SIZE;
    return c;
}

void keyboard_init(void) {
    kb_head = kb_tail = 0;
}
