; ---------------------------------------------------------------------------
; osfmk/x86_64/trampoline.S
;
; AP startup trampoline: 16-bit real mode -> 32-bit protected -> 64-bit long mode
;
; Assemble with:  nasm -f bin trampoline.S -o trampoline.bin
; Loaded at:     physical 0x8000 (below 1 MB, reachable by SIPI)
;
; Shared data at physical 0x8200 (struct tramp_data in smp.h):
;   +0x00  u64  stack    AP kernel stack pointer
;   +0x08  u64  cr3      page table base
;   +0x10  u64  entry    C function to call
;   +0x18  u32  cpu_id   (unused by trampoline, written by kernel)
;   +0x1C  u32  ready    set to 1 by the AP once it is running
; ---------------------------------------------------------------------------

default abs
org 0x8000

; ==== 16-bit Real Mode ======================================================

bits 16
entry16:
    mov byte [0x82F0], 0xAA
    cli
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    mov sp, 0x7C00              ; safe spot below us

    ; Load the 32-bit GDT (at0x8106, pointed to by GDTR at 0x8100)
    lgdt [0x8100]

    ; Enable Protected Mode (CR0.PE)
    mov eax, cr0
    or al, 1
    mov cr0, eax
    mov byte [0x82F0], 0xBB

    ; Far jump to 32-bit code segment
    jmp 0x08:entry32

; ==== GDT (at physical 0x8100) ==============================================
; Layout:
;   0x8100  GDTR (6 bytes)
;   0x8106  GDT entries (4 entries)

align 16
times 0x100 - ($ - $$) db 0    ; pad to offset 0x100 (physical 0x8100)

gdtr32:
    dw 31                       ; limit = 4 entries * 8 - 1 = 31
    dd 0x8106                   ; GDT base (right after GDTR)

tramp_gdt:
    dq 0x0000000000000000       ; 0x00: null descriptor
    dq 0x00CF9A000000FFFF       ; 0x08: 32-bit code  (base 0, limit 4G, exec/read)
    dq 0x00CF92000000FFFF       ; 0x10: 32-bit data  (base 0, limit 4G, read/write)
    dq 0x00AF9A000000FFFF       ; 0x18: 64-bit code  (L=1, base 0, limit 4G)
tramp_gdt_end:

; ==== 32-bit Protected Mode =================================================

bits 32
entry32:
    mov byte [0x82F0], 0xCC
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax

    ; Enable PAE + OSFXSR + OSXMMEXCPT in CR4
    mov eax, cr4
    or eax, (1 << 5) | (1 << 9) | (1 << 10)
    mov cr4, eax

    ; Load CR3 from shared data at physical 0x8208
    mov eax, [0x8208]
    mov cr3, eax

    ; Enable Long Mode Enable (EFER.LME)
    mov ecx, 0xC0000080
    rdmsr
    or eax, (1 << 8)
    wrmsr

    ; Enable paging (CR0.PG) - activates long mode
    mov eax, cr0
    or eax, (1 << 31)
    mov cr0, eax

    ; Far jump to 64-bit code segment
    jmp 0x18:entry64

; ==== 64-bit Long Mode ======================================================

bits 64
entry64:
    mov byte [0x82F0], 0xDD
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax

    ; Load stack pointer from shared data
    mov rsp, [0x8200]

    ; Reload CR3 (identity map is already active, but be explicit)
    mov rax, [0x8208]
    mov cr3, rax

    ; Call the C entry function
    mov byte [0x82F0], 0xEE
    mov rax, [0x8210]
    call rax

    ; Should never return; if it does, halt
.hang:
    cli
    hlt
    jmp .hang
