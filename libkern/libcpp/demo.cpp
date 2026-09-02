#include "libkern/libcpp/libcpp.h"
#include "version.h"

using namespace kernel;

extern "C" void demo(void){

    kernel::klog("cpp_service","from libcpp\n");
    kernel::klog("cpp_service","%s\n",arch);
}
    /*
     * this cpp file isnt important towards osfmk/kern/main.c
     * extern the demo in kern/main.c via 'extern void demo();'
     * theres already in the __init(); the sub init proc
     */
