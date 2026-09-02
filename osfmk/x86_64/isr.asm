; ---------------------------------------------------------------------------
; osfmk/x86_64/isr.asm - x86_64 interrupt, exception, syscall and
; scheduler trampolines.
;
; Long-mode interrupt frame pushed by the CPU (all interrupts):
;   SS, RSP, RFLAGS, CS, RIP          (plus error code for some faults)
;
; Canonical GP-register frame after PUSHALL (qword indices from the
; saved-frame pointer, see proc.c g_syscall_frame comment):
;   f[0]=r15 f[1]=r14 f[2]=r13 f[3]=r12 f[4]=r11 f[5]=r10 f[6]=r9 f[7]=r8
;   f[8]=rdi  f[9]=rsi  f[10]=rbp f[11]=rbx f[12]=rdx f[13]=rcx f[14]=rax
;   f[15]=rip f[16]=cs  f[17]=rflags f[18]=rsp f[19]=ss
; ---------------------------------------------------------------------------

BITS 64

%macro PUSHALL 0
    push rax
    push rcx
    push rdx
    push rbx
    push rbp
    push rsi
    push rdi
    push r8
    push r9
    push r10
    push r11
    push r12
    push r13
    push r14
    push r15
%endmacro

%macro POPALL 0
    pop r15
    pop r14
    pop r13
    pop r12
    pop r11
    pop r10
    pop r9
    pop r8
    pop rdi
    pop rsi
    pop rbp
    pop rbx
    pop rdx
    pop rcx
    pop rax
%endmacro

section .text

global irq0_handler
global irq0_resume
global irq1_handler
global irq12_handler
global sched_switch
global sched_start
global syscall_handler
global exc14_handler
global irq_default_0
global irq_default_1
global irq_default_2
global irq_default_3
global irq_default_4
global irq_default_5
global irq_default_6
global irq_default_7
global irq_default_8
global irq_default_9
global irq_default_10
global irq_default_11
global irq_default_12
global irq_default_13
global irq_default_14
global irq_default_15
global exc0_handler
global exc1_handler
global exc2_handler
global exc3_handler
global exc4_handler
global exc5_handler
global exc6_handler
global exc7_handler
global exc8_handler
global exc9_handler
global exc10_handler
global exc11_handler
global exc12_handler
global exc13_handler
global exc15_handler
global exc16_handler
global exc17_handler
global exc18_handler
global exc19_handler
global exc20_handler
global exc21_handler
global exc22_handler
global exc23_handler
global exc24_handler
global exc25_handler
global exc26_handler
global exc27_handler
global exc28_handler
global exc29_handler
global exc30_handler
global exc31_handler

extern atkbd_handler_irq1
extern atmouse_handler_irq12
extern pit_handler_irq0
extern sched_tick
extern g_cur_task
extern syscall_dispatch
extern g_syscall_frame
extern exc_page_fault
extern kernel_irq_dispatch

; --- timer: tick handler + scheduler hook -----------------------------------

irq0_handler:
    PUSHALL
    call pit_handler_irq0
    mov rax, [rel g_cur_task]
    test rax, rax
    jz .skip_sched
    call sched_switch
.skip_sched:
irq0_resume:
    mov dword [0xFEE000B0], 0     ; LAPIC EOI
    mov al, 0x20
    out 0x20, al
    POPALL
    iretq

; --- scheduler context switch ------------------------------------------------
; struct task layout contract (see init_ram_getty.h):
;   +0  sp   (kernel stack pointer)      - first member
;   +8  cr3                              - second member
;
; Suspend: pushes callee-saved registers, stores RSP into t->sp.
; Resume:  loads new RSP, pops the same registers, rets into the stub.

sched_switch:
    mov r10, [rel g_cur_task]
    push rbx
    push rbp
    push r12
    push r13
    push r14
    push r15
    mov [r10], rsp                 ; save kernel stack pointer
    mov rbx, r10
    call sched_tick                ; may change g_cur_task
    mov rcx, [rel g_cur_task]
    cmp rcx, rbx
    je .switch_same
    cli
    mov rdx, [rcx + 8]             ; t->cr3
    mov cr3, rdx
    mov rsp, [rcx]                 ; t->sp
.resume:
    pop r15
    pop r14
    pop r13
    pop r12
    pop rbp
    pop rbx
    ret
.switch_same:
    add rsp, 48                    ; discard our six pushes
    ret

sched_start:
    cli
    mov rcx, [rel g_cur_task]
    mov rdx, [rcx + 8]
    mov cr3, rdx
    mov rsp, [rcx]
    jmp sched_switch.resume        ; pop regs and return into the task

; --- int $0x80 syscall gate ---------------------------------------------------
; User ABI: rax = number, rdi = a1, rsi = a2, rdx = a3, return in rax.
; Kernel dispatch: syscall_dispatch(a1, a2, a3, num).

syscall_handler:
    PUSHALL
    mov [rel g_syscall_frame], rsp

    mov rdi, [rsp + 8*8]           ; f[8]  = rdi (a1)
    mov rsi, [rsp + 9*8]           ; f[9]  = rsi (a2)
    mov rdx, [rsp + 12*8]          ; f[12] = rdx (a3)
    mov rcx, [rsp + 14*8]          ; f[14] = rax (num)
    call syscall_dispatch

    mov [rsp + 14*8], rax          ; store return value into saved rax
    cmp rax, 0x55550001            ; XKERN_EXEC_MAGIC: jump straight into exec
    jne .syscall_ret
.exec_enter:
    mov rcx, [rel g_cur_task]
    mov rdx, [rcx + 8]
    mov cr3, rdx
    mov rsp, [rcx]
    jmp sched_switch.resume
.syscall_ret:
    POPALL
    iretq

irq1_handler:
    PUSHALL
    call atkbd_handler_irq1
    mov dword [0xFEE000B0], 0     ; LAPIC EOI
    mov al, 0x20
    out 0x20, al
    POPALL
    iretq

irq12_handler:
    PUSHALL
    call atmouse_handler_irq12
    mov dword [0xFEE000B0], 0     ; LAPIC EOI
    mov al, 0x20
    out 0x20, al
    out 0xA0, al
    POPALL
    iretq

%macro DEF_IRQ 1
irq_default_%1:
    PUSHALL
    mov dword [0xFEE000B0], 0     ; LAPIC EOI
    mov al, 0x20
    out 0x20, al
    %if %1 >= 8
        out 0xA0, al
    %endif
    POPALL
    iretq
%endmacro

DEF_IRQ 0
DEF_IRQ 1
DEF_IRQ 2
DEF_IRQ 3
DEF_IRQ 4
DEF_IRQ 5
DEF_IRQ 6
DEF_IRQ 7
DEF_IRQ 8
DEF_IRQ 9
DEF_IRQ 10
DEF_IRQ 11
DEF_IRQ 12
DEF_IRQ 13
DEF_IRQ 14
DEF_IRQ 15

; --- generic, dynamically-dispatched IRQs (for USB et al.) -------------------
; PIC remaps IRQ N to vector 0x20 + N.  Each generated stub passes its vector
; to kernel_irq_dispatch(), which looks up the driver handler and acks the
; interrupt.  kernel_irq_stubs[] lets C install the right gate per IRQ.
; (Kept in .text so it lands in the same mapped segment as the code.)

global kernel_irq_stubs
kernel_irq_stubs:
%assign i 0
%rep 16
    dq irq_disp_%[i]
%assign i i+1
%endrep

%macro DEF_DISP 1
irq_disp_%1:
    PUSHALL
    mov dil, %1 + 0x20             ; vector = 0x20 + irq
    call kernel_irq_dispatch
    POPALL
    iretq
%endmacro

%assign i 0
%rep 16
    DEF_DISP i
%assign i i+1
%endrep

; --- exceptions ---------------------------------------------------------------
; Faults that push an error code: 8, 10-13, 14, 17.  The code is dropped
; before the plain iretq path.  #PF (14) reports rip/cr2 to C.

%macro DEF_exc_no_code 1
exc%1_handler:
    PUSHALL
    POPALL
    iretq
%endmacro

%macro DEF_exc_with_code 1
exc%1_handler:
    PUSHALL
    POPALL
    add rsp, 8                     ; drop error code
    iretq
%endmacro

DEF_exc_no_code 0
DEF_exc_no_code 1
DEF_exc_no_code 2
DEF_exc_no_code 3
DEF_exc_no_code 4
DEF_exc_no_code 5
DEF_exc_no_code 6
DEF_exc_no_code 7
DEF_exc_with_code 8
DEF_exc_no_code 9
DEF_exc_with_code 10
DEF_exc_with_code 11
DEF_exc_with_code 12
DEF_exc_with_code 13
exc14_handler:
    PUSHALL
    mov rdi, [rsp + 15*8]          ; arg1: saved rip
    mov rsi, cr2                   ; arg2: faulting address
    sub rsp, 8                     ; keep alignment for the C call
    call exc_page_fault
    add rsp, 16                    ; drop pad + CPU error code
    POPALL
    iretq
DEF_exc_no_code 15
DEF_exc_no_code 16
DEF_exc_with_code 17
DEF_exc_no_code 18
DEF_exc_no_code 19
DEF_exc_no_code 20
DEF_exc_no_code 21
DEF_exc_no_code 22
DEF_exc_no_code 23
DEF_exc_no_code 24
DEF_exc_no_code 25
DEF_exc_no_code 26
DEF_exc_no_code 27
DEF_exc_no_code 28
DEF_exc_no_code 29
DEF_exc_no_code 30
DEF_exc_no_code 31
