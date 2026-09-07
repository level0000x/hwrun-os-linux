section .text
global arch_setup_context
global arch_context_switch
global arch_task_trampoline
extern task_exit
arch_setup_context:
    mov eax, [esp + 4]
    mov edx, [eax + 8]
    sub edx, 4
    mov ecx, [esp + 12]
    mov [edx], ecx
    sub edx, 4
    mov ecx, [esp + 8]
    mov [edx], ecx
    sub edx, 4
    mov dword [edx], arch_task_trampoline
    mov [eax + 8], edx
    ret

arch_task_trampoline:
    pop eax
    pop edx
    push edx
    call eax
    call task_exit
    cli
.halt:
    hlt
    jmp .halt

arch_context_switch:
    mov eax, [esp + 4]
    mov edx, [esp + 8]
    test eax, eax
    jz .load
    mov [eax + 8], esp
.load:
    mov esp, [edx + 8]
    ret
