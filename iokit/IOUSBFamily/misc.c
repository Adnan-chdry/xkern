/*
 * sub entry for all the usb devices and exposure to the kernel entry point
 * uses ldm id to encrypt all the calls
 * gives up features for reading usb signals and kill usb ports.
 * xkern 26.0.8
 */

#include <klog.h>
#include "usb.h"

void usb_read(const *dev,const *port){
    klog("usb.0","kernel.read(usb_read(%s.%s))",dev,port);
    /*
     * logic
     */
}
