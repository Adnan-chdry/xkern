/*
 * written for xkern only
 * you cant add a change to this
 */

#include "cpu.h"
/*follwing headers needs to be installed from header-install*/
#include <xkern/include/types.h>
#include <xkern/libkern/libkern/klog.h>
#include <xkern/libkern/libkern/klibc.h>

static void cpuid(
    u32 leaf,
    u32 subleaf,
    u32 *eax,
    u32 *ebx,
    u32 *ecx,
    u32 *edx
)
{
    __asm__ volatile(
        "cpuid":
        "=a" (*eax),
        "=b" (*ebx),
        "=c" (*ecx),
        "=d" (*edx):
        "a" (leaf),
        "c" (subleaf)
    );
}

void cpu_get_info(cpu_info_t *info){
    uint32_t eax;
    uint32_t ebx;
    uint32_t ecx;
    uint32_t edx;
    uint32_t max_extended;

    /*routine for cpu vendor*/
    cpuid(0,0, &eax, &ebx, &ecx, &edx);
    info->vendor[0]=(char)(ebx);
    info->vendor[1]=(char)(ebx>>8);
    info->vendor[2]=(char)(ebx>>16);
    info->vendor[3]=(char)(ebx>>24);
    info->vendor[4]=(char)(edx);
    info->vendor[5]=(char)(edx>>8);
    info->vendor[6]=(char)(edx>>16);
    info->vendor[7]=(char)(edx>>24);
    info->vendor[8]=(char)(ecx>>8);
    info->vendor[9]=(char)(ecx>>16);
    info->vendor[8]=(char)(ecx>>24);
    info->vendor[12]= '\0';

    cpuid(
        0x80000000,
        0,
        &max_extended,
        &ebx,
        &ecx,
        &edx
    );
    info->name[0]='\0';
    if (max_extended >= 0x80000004) {
            uint32_t *name;

            name = (uint32_t *)info->name;

            cpuid(
                0x80000002,
                0,
                &name[0],
                &name[1],
                &name[2],
                &name[3]
            );

            cpuid(
                0x80000003,
                0,
                &name[4],
                &name[5],
                &name[6],
                &name[7]
            );

            cpuid(
                0x80000004,
                0,
                &name[8],
                &name[9],
                &name[10],
                &name[11]
            );

            info->name[48] = '\0';
        }

}
