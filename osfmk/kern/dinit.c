#include "logger.h"
#include "vga.h"
#include "klog.h"
#include "multiboot.h"
#include "IOGraphicsFamily/fb.h"
#include "IOGraphicsFamily/font.h"
#include "stdio.h"
#include "paging.h"
#include "kernel.h"
#include "klibc.h"

void dinit_init(void) {
    klibc.printf("buffer available in memory worker started");
    vga_init();
    klog("dinit","display_init_system");
}
