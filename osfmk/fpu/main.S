BITS 32

global fpu_init
global fpu_has_sse
global fpu_has_sse2

section .text

; int fpu_has_sse(void)

fpu_has_sse:
    push ebx

    ; Check CPUID availability first.
    pushfd
    pop eax
    mov ecx, eax

    xor eax, 1
    push eax
    popfd

    pushfd
    pop eax
    xor eax, ecx

    ; ID flag didn't change -> no CPUID
    jz .no_sse

    xor eax, eax
    cpuid

    ; CPUID leaf 1
    mov eax, 1
    cpuid

    ; EDX bit 25 = SSE
    test edx, (1 << 25)
    jz .no_sse

    mov eax, 1
    pop ebx
    ret

.no_sse:
    xor eax, eax
    pop ebx
    ret


; int fpu_has_sse2(void)

fpu_has_sse2:
    push ebx

    ; Check CPUID availability.
    pushfd
    pop eax
    mov ecx, eax

    xor eax, 1
    push eax
    popfd

    pushfd
    pop eax
    xor eax, ecx

    jz .no_sse2

    xor eax, eax
    cpuid

    ; CPUID leaf 1
    mov eax, 1
    cpuid

    ; EDX bit 26 = SSE2
    test edx, (1 << 26)
    jz .no_sse2

    mov eax, 1
    pop ebx
    ret

.no_sse2:
    xor eax, eax
    pop ebx
    ret


; ------------------------------------------------------------
; void fpu_init(void)
;
; Initialize x87 and enable SSE/SSE2 OS support.
; ------------------------------------------------------------
fpu_init:
    push eax

    ; --------------------------------------------------------
    ; CR0
    ; --------------------------------------------------------
    mov eax, cr0

    ; EM = 0
    ; Don't emulate x87.
    and eax, ~(1 << 2)

    ; MP = 1
    ; Monitor coprocessor.
    or eax, (1 << 1)

    ; TS = 0
    ; Don't trap FPU instructions during initialization.
    and eax, ~(1 << 3)

    mov cr0, eax

    ; --------------------------------------------------------
    ; CR4
    ; --------------------------------------------------------
    mov eax, cr4

    ; OSFXSR = 1
    ; OS supports FXSAVE/FXRSTOR and SSE state.
    or eax, (1 << 9)

    ; OSXMMEXCPT = 1
    ; OS supports unmasked SSE exceptions.
    or eax, (1 << 10)

    mov cr4, eax

    ; Reset x87 state.
    fninit

    pop eax
    ret