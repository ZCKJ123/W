default rel
global start
global wndproc
extern RegisterClassA
extern CreateWindowExA
extern ShowWindow
extern GetMessageA
extern TranslateMessage
extern DispatchMessageA
extern DefWindowProcA
extern PostQuitMessage
extern GetModuleHandleA
extern BeginPaint
extern EndPaint
extern ReleaseDC
extern StretchDIBits
extern SetTimer
extern InvalidateRect
extern PeekMessageA
extern SendMessageA
extern Sleep
extern ExitProcess

section .text
wndproc:
    cmp edx, 0x0F
    je .paint
    cmp edx, 0x113
    je .invalidate
    cmp edx, 0x14
    je .erase
    cmp edx, 2
    je .destroy
    jmp DefWindowProcA
.invalidate:
    push rbx
    push r13
    sub rsp, 0x28
    mov rbx, rcx
    mov r13d, [rel frame]
    xor r13d, 1
    mov [rel frame], r13d
    lea rdi, [rel pixels]
    mov eax, 0x00FF8040
    test r13d, r13d
    jz .fill
    mov eax, 0x002060C0
.fill:
    mov r13d, 320*180
    rep stosd
    mov rcx, rbx
    xor edx, edx
    xor r8d, r8d
    call InvalidateRect
    add rsp, 0x28
    pop r13
    pop rbx
    xor eax, eax
    ret
.erase:
    mov eax, 1
    ret
.destroy:
    sub rsp, 0x28
    xor ecx, ecx
    call PostQuitMessage
    add rsp, 0x28
    xor eax, eax
    ret
.paint:
    push rbx
    push r12
    sub rsp, 0x68
    mov rbx, rcx
    lea rdx, [rel ps]
    call BeginPaint
    mov r12, rax
    mov rcx, r12
    xor edx, edx
    xor r8d, r8d
    mov r9d, 640
    mov qword [rsp+0x20], 360
    mov qword [rsp+0x28], 0
    mov qword [rsp+0x30], 0
    mov qword [rsp+0x38], 320
    mov qword [rsp+0x40], 180
    mov eax, [rel frame]
    and eax, 1
    imul eax, 320*180*4
    lea r10, [rel pixels]
    add r10, rax
    mov [rsp+0x48], r10
    lea rax, [rel bmi]
    mov [rsp+0x50], rax
    mov qword [rsp+0x58], 0
    mov qword [rsp+0x60], 0x00CC0020
    call StretchDIBits
    mov rcx, rbx
    lea rdx, [rel ps]
    call EndPaint
    xor dword [rel frame], 1
    mov rcx, rbx
    xor edx, edx
    xor r8d, r8d
    call InvalidateRect
    add rsp, 0x68
    pop r12
    pop rbx
    xor eax, eax
    ret

start:
    sub rsp, 0xA8
    xor ecx, ecx
    call GetModuleHandleA
    mov r15, rax

    mov dword [rel wc], 3
    lea rax, [rel wndproc]
    mov [rel wc+8], rax
    mov qword [rel wc+16], 0
    mov [rel wc+24], r15
    mov qword [rel wc+32], 0
    mov qword [rel wc+40], 0
    mov qword [rel wc+48], 0
    mov qword [rel wc+56], 0
    lea rax, [rel classname]
    mov [rel wc+64], rax
    lea rcx, [rel wc]
    call RegisterClassA

    xor ecx, ecx
    lea rdx, [rel classname]
    lea r8, [rel title]
    mov r9d, 0x00CF0000
    mov qword [rsp+0x20], 100
    mov qword [rsp+0x28], 100
    mov qword [rsp+0x30], 640
    mov qword [rsp+0x38], 360
    mov qword [rsp+0x40], 0
    mov qword [rsp+0x48], 0
    mov qword [rsp+0x50], r15
    mov qword [rsp+0x58], 0
    call CreateWindowExA
    mov rbx, rax


    mov rcx, rbx
    mov edx, 5
    call ShowWindow

loopmsg:
    lea rcx, [rsp+0x20]
    xor edx, edx
    xor r8d, r8d
    xor r9d, r9d
    mov qword [rsp+0x20], 1
    call PeekMessageA
    test eax, eax
    jz .draw
    lea rcx, [rsp+0x20]
    call TranslateMessage
    lea rcx, [rsp+0x20]
    call DispatchMessageA
.draw:
    mov ecx, 16
    mov ecx, 16
    call Sleep
    jmp loopmsg
done:
    xor ecx, ecx
    call ExitProcess

section .data
classname: db "WenClass",0
title:     db "Wen Class",0
wc: times 72 db 0
ps: times 72 db 0
bmi:
    dd 40
    dd 320
    dd -180
    dw 1
    dw 32
    dd 0
    dd 320*180*4
    times 16 db 0
pixels: times 320*180*4*2 db 0
frame: dd 0
