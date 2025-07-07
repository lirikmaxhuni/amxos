section .multiboot
align 4
    MULTIBOOT_MAGIC    equ 0x1BADB002
    MULTIBOOT_FLAGS    equ 0x3
    MULTIBOOT_CHECKSUM equ -(MULTIBOOT_MAGIC + MULTIBOOT_FLAGS)

    dd MULTIBOOT_MAGIC
    dd MULTIBOOT_FLAGS
    dd MULTIBOOT_CHECKSUM

section .gdt
gdt_start:
    dq 0x0000000000000000     ; Null descriptor
    dq 0x00cf9a000000ffff     ; Code segment (base=0, limit=4GB, code)
    dq 0x00cf92000000ffff     ; Data segment (base=0, limit=4GB, data)
gdt_end:

gdt_descriptor:
    dw gdt_end - gdt_start - 1
    dd gdt_start

global _start
section .text
_start:
    cli
    mov esp, stack_top
    lgdt [gdt_descriptor]
    mov ax, 0x10         ; Data segment selector (2nd entry, index 2*8=0x10)
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    mov eax, 0x08        ; Code segment selector (1st entry, index 1*8=0x08)
    push eax
    push next
    retf
next:
    extern kmain
    call kmain
    cli
.hang:
    hlt
    jmp .hang

section .bss
align 16
global stack_bottom
global stack_top
stack_bottom:
    resb 16384
stack_top:

; Minimal keyboard interrupt handler (IRQ1, interrupt vector 0x21)
global asm_keyboard_on_interrupt
extern keyboard_interrupt_handler

section .text
align 4
global asm_keyboard_on_interrupt
asm_keyboard_on_interrupt:
    pusha
    push ds
    push es
    push fs
    push gs
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    in al, 0x60
    movzx eax, al
    mov ecx, esp
    and esp, 0xFFFFFFF0
    push eax
    call keyboard_interrupt_handler
    add esp, 4
    mov esp, ecx
    pop gs
    pop fs
    pop es
    pop ds
    mov al, 0x20
    out 0x20, al
    popa
    iret

align 4
global default_handler
default_handler:
    pusha
    mov al, 0x20
    out 0x20, al
    popa
    iret

align 4
global pure_asm_keyboard_handler
pure_asm_keyboard_handler:
    pusha
    mov al, 0x20
    out 0x20, al
    popa
    iret

; Timer interrupt handler (IRQ0, interrupt vector 0x20)
global asm_timer_on_interrupt
extern timer_interrupt_handler

section .text
align 4
global asm_timer_on_interrupt
asm_timer_on_interrupt:
    pusha
    push ds
    push es
    push fs
    push gs
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    call timer_interrupt_handler
    mov al, 0x20
    out 0x20, al
    pop gs
    pop fs
    pop es
    pop ds
    popa
    iret

; Page fault handler (interrupt 0xE)
global asm_page_fault_handler
extern page_fault_handler

section .text
align 4
global asm_page_fault_handler
asm_page_fault_handler:
    cli
    mov byte [0xB8000], 'F'
    mov byte [0xB8001], 0x4F
    pusha
    mov eax, [esp + 32] ; error code after pusha
    push eax
    call page_fault_handler
    add esp, 4
    popa
    iret

global asm_double_fault_handler
asm_double_fault_handler:
    mov byte [0xB8002], 'D'
    mov byte [0xB8003], 0x4C
    cli
    hlt