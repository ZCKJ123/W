bits 64
org 0x4000
default rel

start:
    cmp edx, 0x10
    je close
    cmp edx, 0x0F
    jne retzero
    sub rsp, 0x78
    mov [rsp+0x68], rcx
    call [rel 0x30C8]
    mov rcx, rax
    xor edx, edx
    xor r8d, r8d
    mov r9d, 640
    mov qword [rsp+0x20], 360
    mov qword [rsp+0x28], 0
    mov qword [rsp+0x30], 0
    mov qword [rsp+0x38], 320
    mov qword [rsp+0x40], 180
    movzx eax, byte [rel 0x4100]
    imul eax, eax, 0x38400
    mov rdx, 0x140005100
    add rax, rdx
    mov [rsp+0x48], rax
    xor byte [rel 0x4100], 1
    mov rax, 0x140005000
    mov [rsp+0x50], rax
    mov qword [rsp+0x58], 0
    mov qword [rsp+0x60], 0x00CC0020
    call [rel 0x30E0]
    mov rcx, [rsp+0x68]
    xor edx, edx
    xor r8d, r8d
    call [rel 0x30D0]
    add rsp, 0x78
retzero:
    xor eax, eax
    ret
close:
    sub rsp, 0x28
    xor ecx, ecx
    call [rel 0x30F0]
    int3
