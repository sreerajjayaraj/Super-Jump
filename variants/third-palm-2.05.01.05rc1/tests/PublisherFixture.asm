PUBLIC FixturePublisher
PUBLIC FixtureCallFirst
PUBLIC FixtureCallSecond
PUBLIC FixtureCallOther
PUBLIC FixtureReturnFirst
PUBLIC FixtureReturnSecond
PUBLIC FixtureMotion
PUBLIC FixtureMotionSite

.code

FixturePublisher PROC
    mov r11, rsp
    mov [r11+8], rbx
    mov rax, rcx
    add rax, r9
    ret
FixturePublisher ENDP

FixtureCaller MACRO function, afterCall
function PROC FRAME
    sub rsp, 28h
    .allocstack 28h
    .endprolog
    call FixturePublisher
afterCall LABEL BYTE
    add rsp, 28h
    ret
function ENDP
ENDM

FixtureCaller FixtureCallFirst, FixtureReturnFirst
FixtureCaller FixtureCallSecond, FixtureReturnSecond
FixtureCaller FixtureCallOther, FixtureReturnOther

FixtureMotion PROC
    push rbx
    push r13
    sub rsp,16
    movups [rsp],xmm6
    mov rbx,rcx
    lea r13,[rcx+90h]
    movups xmm6,[rdx]
    cmp r8d,2
FixtureMotionSite LABEL BYTE
    db 074h,03Eh
    movaps xmm0,xmm6
    mov eax,1
    jmp short StoreMotion
    db (FixtureMotionSite + 40h - $) dup (90h)
    movaps xmm0,xmm6
    mov eax,2
StoreMotion:
    movups [rdx],xmm0
    movups xmm6,[rsp]
    add rsp,16
    pop r13
    pop rbx
    ret
FixtureMotion ENDP

END
