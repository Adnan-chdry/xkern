#ifndef CPU_H
#define CPU_H

#include "types.h"

#define CPU_VENDOR_LEN 13

extern char cpu_vendor[CPU_VENDOR_LEN];
extern u32 cpu_signature;

void cpuid(u32 leaf, u32 *eax, u32 *ebx, u32 *ecx, u32 *edx);
void cpu_init(void);

#endif
