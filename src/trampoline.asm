section .text
global task_trampoline
extern task_exit

task_trampoline:
    mov eax, [esp+4]    ; Get entry function pointer (argument to trampoline)
    call eax            ; Call the entry function
    call task_exit      ; If entry returns, exit the task
    hlt 