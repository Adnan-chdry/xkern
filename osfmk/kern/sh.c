#include "panic.h"
#include <xkern/libkern/libkern/klibc.h>
#include <xkern/osfmk/kern/version.h>

int sh_main(){
    char i[256] = {0};  // Initialize to zero
    int ret;
    klibc.printf("[%s #] ",ostype);
    klibc.scanf("%255s", i);
}
