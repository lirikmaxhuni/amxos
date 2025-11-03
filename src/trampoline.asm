section .text
global task_trampoline
extern task_exit

task_trampoline:
    mov byte [0xB8000], 0x23 ; '#'
    mov byte [0xB8001], 0x4E
    pop eax            ; Pop entry function pointer into eax
    call eax           ; Call the entry function
    call task_exit     ; If entry returns, exit the task
    hlt                ; Should never reach here 