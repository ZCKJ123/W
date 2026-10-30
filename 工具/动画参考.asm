default rel
global start
global draw
extern CreateWindowExA
extern ShowWindow
extern GetMessageA
extern TranslateMessage
extern DispatchMessageA
extern GetDC
extern ReleaseDC
extern StretchDIBits
extern ExitProcess

section .text
start:
    sub rsp, 0x68
    xor ecx, ecx
    lea rdx, [rel classname]
    lea r8,  [rel title]
    mov r9d, 0x00CF0000
    mov qword [rsp+0x20], 100
    mov qword [rsp+0x28], 100
    mov qword [rsp+0x30], 640
    mov qword [rsp+0x38], 360
    mov qword [rsp+0x40], 0
    mov qword [rsp+0x48], 0
    mov qword [rsp+0x50], 0
    mov qword [rsp+0x58], 0
    call CreateWindowExA
    mov rbx, rax
    mov rcx, rbx
    mov edx, 5
    call ShowWindow
    lea rdi, [rel pixels]
    mov eax, 0x00FF8040
    mov ecx, 160*90
    rep stosd
    mov rcx, rbx
    call GetDC
    xor ecx, ecx
    test rax, rax
    setne cl
    call ExitProcess

    lea rdi, [rel pixels]
    mov eax, 0x00FF8040
    mov ecx, 160*90
    rep stosd
    lea rdi, [rel pixels + 160*90*4]
    mov eax, 0x00C05020
    mov ecx, 160*90
    rep stosd

    jmp draw
loopmsg:
    lea rcx, [rsp+0x20]
    xor edx, edx
    xor r8d, r8d
    xor r9d, r9d
    call GetMessageA
    test eax, eax
    jle done
    lea rcx, [rsp+0x20]
    call TranslateMessage
    lea rcx, [rsp+0x20]
    call DispatchMessageA

draw:
    lea rdi, [rel pixels]
    mov eax, 0x00FF8040
    mov ecx, 160*90
    rep stosd
    mov rcx, rbx
    call GetDC
    mov r12, rax
    mov rcx, r12
    xor edx, edx
    xor r8d, r8d
    mov r9d, 640
    mov qword [rsp+0x20], 360
    mov qword [rsp+0x28], 0
    mov qword [rsp+0x30], 0
    mov qword [rsp+0x38], 160
    mov qword [rsp+0x40], 90
    mov eax, [rel frame]
    mov ecx, eax
    inc eax
    mov [rel frame], eax
    and ecx, 1
    imul ecx, 160*90*4
    lea rax, [rel pixels]
    add rax, rcx
    mov [rsp+0x48], rax
    lea rax, [rel bmi]
    mov [rsp+0x50], rax
    mov qword [rsp+0x58], 0
    mov qword [rsp+0x60], 0x00CC0020
    call StretchDIBits
    mov rcx, rbx
    mov rdx, r12
    call ReleaseDC
    jmp draw

done:
    xor ecx, ecx
    call ExitProcess

section .data
classname: db "STATIC",0
title:     db "Wen Anim NASM",0
bmi:
    dd 40
    dd 160
    dd -90
    dw 1
    dw 32
    dd 0
    dd 160*90*4
    times 16 db 0
pixels: times 160*90*4*2 db 0
frame: dd 0
