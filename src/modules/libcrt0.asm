[SECTION .text]
[BITS 32]
extern main
; extern exit

global _start

_start:
    push eax
    push ecx
    call main
    ;push eax
    ;call exit

    hlt

