/*
 *  Have to modify the libkern to support FPU based work
 * */

#include "fpu.h"
#include <types.h>
#include <klog.h>
#include <klibc.h>
#include <id.h> //new addition devkits/id.h

static const struct IDdriver id={
    .name = "xk_fpu",
    .ver = "26",
    .type = "float point unit"
};


//align 16
static __attribute__((aligned(16))) u8 fpu_state[512];

void kernel_fpu_init(void)
{
    klog_lvl(KLOG_INFO,"def_mod","loading %s ver <%s>",id.name,id.ver);
    //all those sse checkers are gone kernel cant be ran without it
    fpu_init();
    klog_lvl(KLOG_NOTICE, "fpu","fpu x87 enabled");


}

void fpu_save(void)
{
    __asm__ volatile ("fxsave %0" : "=m"(fpu_state));
}

void fpu_restore(void)
{
    __asm__ volatile ("fxrstor %0" :: "m"(fpu_state));
}
