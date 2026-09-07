section .multiboot
align 4
dd 0x1BADB002
dd 0x03
dd -(0x1BADB002 + 0x03)

section .text
global _start
extern kmain
extern bss_start
extern bss_end

_start:
    mov esp, stack_top
    cld
    mov edi, bss_start
    mov ecx, bss_end
    sub ecx, edi
    xor eax, eax
    rep stosb
    push ebx
    push eax
    call kmain
    cli
.halt:
    hlt
    jmp .halt

section .bss
align 16
stack_bottom:
    resb 16384
stack_top:
