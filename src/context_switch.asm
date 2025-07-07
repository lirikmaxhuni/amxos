section .text
    global context_switch
    ; void context_switch(cpu_context_t *old, cpu_context_t *new)
    ; old: [esp+4], new: [esp+8]
context_switch:
    ; Save all general-purpose registers to *old
    mov eax, [esp+4]      ; eax = old
    mov [eax],    edi     ; old->edi
    mov [eax+4],  esi     ; old->esi
    mov [eax+8],  ebx     ; old->ebx
    mov [eax+12], ebp     ; old->ebp
    mov [eax+16], esp     ; old->esp (current stack pointer)
    mov edx, [esp]        ; edx = return address
    mov [eax+20], edx     ; old->eip (return address)

    ; Load general-purpose registers from *new
    mov eax, [esp+8]      ; eax = new
    mov edi, [eax]        ; new->edi
    mov esi, [eax+4]      ; new->esi
    mov ebx, [eax+8]      ; new->ebx
    mov ebp, [eax+12]     ; new->ebp
    mov esp, [eax+16]     ; new->esp
    mov ecx, [eax+20]     ; new->eip
    jmp ecx               ; jump to new eip 