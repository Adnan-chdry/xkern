#include "cpu.h"
#include "types.h"

char cpu_vendor[CPU_VENDOR_LEN] = "unknown";
u32 cpu_signature = 0;

void cpuid(u32 leaf, u32 *eax, u32 *ebx, u32 *ecx, u32 *edx)
{
    __asm__ volatile(
        "cpuid"
        : "=a"(*eax), "=b"(*ebx), "=c"(*ecx), "=d"(*edx)
        : "a"(leaf)
    );
}

void cpu_init(void)
{
    u32 ebx, ecx, edx;

    cpuid(0, &cpu_signature, &ebx, &ecx, &edx);

    *(u32 *)&cpu_vendor[0] = ebx;
    *(u32 *)&cpu_vendor[4] = edx;
    *(u32 *)&cpu_vendor[8] = ecx;
    cpu_vendor[12] = '\0';
}
