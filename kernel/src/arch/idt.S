section .text
global idt_entries
global idt_syscall_entry
extern irq_handler
extern exception_handler
extern syscall_handler
%macro irq_stub 1
irq_stub_%1:
    pushad
    push %1
    call irq_handler
    add esp, 4
    popad
    iret
%endmacro
%macro exception_stub 2
exception_stub_%1:
    pushad
    push dword 0
    push %1
    call exception_handler
    add esp, 8
    popad
%if %2
    add esp, 4
%endif
    iret
%endmacro
exception_stub 0, 0
exception_stub 1, 0
exception_stub 2, 0
exception_stub 3, 0
exception_stub 4, 0
exception_stub 5, 0
exception_stub 6, 0
exception_stub 7, 0
exception_stub 8, 1
exception_stub 9, 0
exception_stub 10, 1
exception_stub 11, 1
exception_stub 12, 1
exception_stub 13, 1
exception_stub 14, 1
exception_stub 15, 0
exception_stub 16, 0
exception_stub 17, 1
exception_stub 18, 0
exception_stub 19, 0
exception_stub 20, 0
exception_stub 21, 0
exception_stub 22, 0
exception_stub 23, 0
exception_stub 24, 0
exception_stub 25, 0
exception_stub 26, 0
exception_stub 27, 0
exception_stub 28, 0
exception_stub 29, 1
exception_stub 30, 1
exception_stub 31, 0
%assign i 32
%rep 16
irq_stub i
%assign i i+1
%endrep

section .data
idt_entries:
%assign i 0
%rep 48
%if i < 32
    dd exception_stub_%i
%else
    dd irq_stub_%i
%endif
%assign i i+1
%endrep

section .text
idt_syscall_entry:
    pushad
    push edx
    push ecx
    push ebx
    push eax
    call syscall_handler
    add esp, 16
    mov [esp + 28], eax
    popad
    iret
