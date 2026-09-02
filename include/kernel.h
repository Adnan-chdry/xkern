#ifndef KERNEL_H
#define KERNEL_H
#include "multiboot.h"
#include "types.h"
void kernel_main(u64 mbi_phys);

/* kernel version globals (osfmk/kern/version.c) */
extern const char version[];
extern const char ostype[];
extern const char osrelease[];
extern const int  osrelease_major;
extern const int  osrelease_minor;
extern const int  osrelease_rev;

#endif