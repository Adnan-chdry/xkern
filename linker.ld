ENTRY(_start)

SECTIONS {
    . = 1M;

    _kernel_start = .;

    /* Multiboot 2 header must be within the first 32 KiB: keep it first */
    .multiboot : ALIGN(8) {
        KEEP(*(.multiboot))
    }

    .text : ALIGN(4K) {
        *(.text.boot)
        *(.text.*)
        *(.text)
    }

    .rodata : ALIGN(4K) {
        *(.rodata)
        *(.rodata.*)
    }

    .data : ALIGN(4K) {
        *(.data)
        *(.data.*)
    }

    /* C++ static constructors / destructors (Itanium ABI) */
    .init_array : ALIGN(8) {
        __init_array_start = .;
        KEEP(*(.init_array))
        KEEP(*(SORT_BY_INIT_PRIORITY(.init_array.*)))
        __init_array_end = .;
    }

    .fini_array : ALIGN(8) {
        __fini_array_start = .;
        KEEP(*(.fini_array))
        KEEP(*(SORT_BY_INIT_PRIORITY(.fini_array.*)))
        __fini_array_end = .;
    }

    .bss : ALIGN(4K) {
        *(COMMON)
        *(.bss)
        *(.bss.*)
    }

    /*
     * Embedded install payload (bootable install image).  Written by
     * scripts/gen_payload.sh; see devkits/gpukit/payload/install_payload.S.
     * Kept after .bss in its own loadable segment so the plain kernel
     * (no payload) and the install kernel (payload embedded) differ only
     * by this section's file content.
     */
    .install_payload : ALIGN(16) {
        KEEP(*(.install_payload))
    }

    /DISCARD/ : {
        *(.eh_frame*)
        *(.note*)
        *(.comment)
    }

    _kernel_end = .;
}
