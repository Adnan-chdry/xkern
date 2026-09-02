; ---------------------------------------------------------------------------
; pexpert/x86_64/boot.asm - xkern x86_64 boot entry (Multiboot 2)
;
; GRUB enters here in 32-bit protected mode with:
;   EAX = 0x36D76289  (Multiboot 2 bootloader magic)
;   EBX = physical address of the Multiboot 2 information structure
;
; This trampoline:
;   1. saves the MBI pointer,
;   2. builds identity page tables covering the low 4 GiB with 2 MiB pages
;      (kernel is linked at 1M, framebuffer and ACPI live below 4G),
;   3. enables PAE + LME + PG and far jumps into long mode,
;   4. calls kernel_main(mbi_phys) with the SysV first argument in RDI.
; ---------------------------------------------------------------------------

%define MB2_MAGIC        0xE85250D6
%define MB2_ARCH_I386    0
%define MB2_HDR_END_TAG  0
%define MB2_HDR_FB_TAG   5

%define PRESENT_WRITE    0x003          ; present | writable
%define PRESENT_WRITE_2M 0x083          ; present | writable | 2MiB page

; --- Multiboot 2 header ----------------------------------------------------
; Must be 64-bit aligned within the first 32768 bytes of the image.

section .multiboot
align 8
mb2_header:
    dd MB2_MAGIC
    dd MB2_ARCH_I386
    dd mb2_header_end - mb2_header
    dd -(MB2_MAGIC + MB2_ARCH_I386 + (mb2_header_end - mb2_header))

    ; framebuffer request tag: no size preference - inherit the mode GRUB
    ; programmed for gfxterm (grub.cfg sets gfxmode + gfxpayload=keep)
align 8
    dd MB2_HDR_FB_TAG           ; type = 5
    dd 20                       ; size
    dd 0                        ; width  (0 = no preference)
    dd 0                        ; height
    dd 0                        ; depth

align 8
    dd MB2_HDR_END_TAG          ; end tag
    dd 8
mb2_header_end:

; --- 32-bit entry ----------------------------------------------------------

section .text.boot
bits 32

global _start
extern boot_run

CODE64_SEL equ 0x08
DATA64_SEL equ 0x10

_start:
    cli
    cld

    ; sanity: only answer a real Multiboot 2 bootloader
    cmp eax, 0x36D76289
    jne .no_mb2

    mov [mbi_phys], ebx         ; save MBI physical address

    mov esp, stack_top_32
    and esp, 0xFFFFFFF0

    ; ---- identity-map the low 4 GiB with 2 MiB pages -------------------
    ; PML4[0] -> PDPT, PDPT[i] -> PD_i, PD_i[j] = (i<<30 | j<<21) | 2M flag
    mov eax, pdpt
    or eax, PRESENT_WRITE
    mov [pml4], eax             ; PML4[0] = pdpt | P|W   (high dword already 0)

    mov edi, 0                  ; i = PDPT index / PD number
.fill_pdpt:
    mov eax, pd0                ; pd tables are consecutive: pd0..pd3
    mov ecx, edi
    shl ecx, 12                 ; each PD is one 4K page
    add eax, ecx
    or eax, PRESENT_WRITE
    mov [pdpt + edi*8], eax     ; PDPT[i] = pd_i | P|W

    mov eax, pd0
    add eax, ecx                ; edi-th PD base
    mov edx, 512                ; 512 entries per PD
    mov ebx, edi                ; base offset of this 1 GiB region
    shl ebx, 30                 ; i * 1GiB
.fill_pd:
    mov [eax], ebx              ; entry = phys base of this 2 MiB page
    or dword [eax], PRESENT_WRITE_2M
    add eax, 8
    add ebx, 0x200000           ; next 2 MiB page
    dec edx
    jnz .fill_pd

    inc edi
    cmp edi, 4                  ; 4 GiB covered
    jb .fill_pdpt

    ; ---- enable long mode ----------------------------------------------
    mov eax, pml4
    mov cr3, eax

    mov eax, cr4
    or eax, (1 << 5)            ; PAE
    or eax, (1 << 9) | (1 << 10); OSFXSR | OSXMMEXCPT (SSE available)
    mov cr4, eax

    mov ecx, 0xC0000080         ; EFER MSR
    rdmsr
    or eax, (1 << 8)            ; LME
    wrmsr

    lgdt [gdtr32]

    mov eax, cr0
    or eax, (1 << 31)           ; PG (PE already on under GRUB)
    mov cr0, eax

    jmp CODE64_SEL:long_mode

.no_mb2:
    hlt
    jmp .no_mb2

; --- 64-bit land -----------------------------------------------------------

bits 64
long_mode:
    mov ax, DATA64_SEL
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax

    mov rsp, stack_top
    xor rbp, rbp

    mov edi, [mbi_phys]         ; MBI phys (< 4G, identity mapped), zero-extends

    call boot_run

.hang:
    cli
    hlt
    jmp .hang

; --- GDT -------------------------------------------------------------------

section .rodata
align 8
gdt64:
    dq 0x0000000000000000       ; 0x00 null
    dq 0x00AF9A000000FFFF       ; 0x08 code: L=1 D=0, base 0 limit max
    dq 0x00CF92000000FFFF       ; 0x10 data: base 0 limit 4G
gdt64_end:

align 8
gdtr32:                         ; 6-byte pseudo descriptor for the 32-bit lgdt
    dw gdt64_end - gdt64 - 1
    dd gdt64

section .data
align 8
mbi_phys:
    dd 0

; --- boot allocations (bss: zero-filled by the ELF loader) -----------------

section .bss
align 4096
pml4:   resb 4096               ; PML4
pdpt:   resb 4096               ; PDPT for PML4[0]
pd0:    resb 4096 * 4           ; 4 PDs -> 4 GiB of 2 MiB pages

align 16
stack_bottom_32:
    resb 16384
stack_top_32:

stack_bottom:
    resb 65536
stack_top:
