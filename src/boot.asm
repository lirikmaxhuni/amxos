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
    dq 0x00cf9a000000ffff     ; Code segment
    dq 0x00cf92000000ffff     ; Data segment
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
global stack_top
stack_bottom:
    resb 16384
stack_top:

; Keyboard interrupt handler (IRQ1, interrupt vector 0x21)
global keyboard_handler
extern keyboard_on_interrupt

global asm_keyboard_on_interrupt
asm_keyboard_on_interrupt:
    pusha
    mov eax, 0xB8000
    add eax, 4000           ; 80*25*2 = 4000 bytes for 25 lines
    sub eax, 2              ; Last character cell
    mov byte [eax], 'Z'
    mov byte [eax+1], 0x2E
    mov ax, ds
    mov bx, 0xB8000
    add bx, 188
    mov byte [bx], al
    mov byte [bx+1], 0x2E
    mov byte [bx+2], ah
    mov byte [bx+3], 0x2E
    popa
    ret

keyboard_handler:
    cli
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
    mov eax, 0xB8000
    add eax, 4000           ; 80*25*2 = 4000 bytes for 25 lines
    sub eax, 2              ; Last character cell
    mov byte [eax], 'Z'
    mov byte [eax+1], 0x2E

    pop gs
    pop fs
    pop es
    pop ds
    mov al, 0x20
    out 0x20, al
    popa
    iret

global default_handler
default_handler:
    pusha
    mov eax, 0xB8000
    mov byte [eax], 'X'
    mov byte [eax+1], 0x4F
    mov al, 0x20
    out 0x20, al
    mov al, 0x20
    out 0xA0, al
    popa
    iret

global exception_handler
exception_handler:
    pusha
    mov eax, 0xB8000
    mov byte [eax+4], 'E'   ; Write 'E' at position 2 (after 'Y')
    mov byte [eax+5], 0x1F
    add esp, 4              ; Remove error code if present
    mov al, 0x20
    out 0x20, al
    popa
    iret