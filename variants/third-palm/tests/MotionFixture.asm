PUBLIC MotionFixture
PUBLIC MotionFixtureSite
.code
MotionFixture PROC
    push rbx
    push r13
    sub rsp,16
    movups [rsp],xmm6
    mov rbx,rcx
    lea r13,[rcx+90h]
    movups xmm6,[rdx]
    cmp r8d,2
MotionFixtureSite LABEL BYTE
    db 074h,03Eh
    movaps xmm0,xmm6
    mov eax,1
    jmp short fixture_store
    db (MotionFixtureSite + 40h - $) dup (90h)
    movaps xmm0,xmm6
    mov eax,2
fixture_store:
    movups [rdx],xmm0
    movups xmm6,[rsp]
    add rsp,16
    pop r13
    pop rbx
    ret
MotionFixture ENDP
END
