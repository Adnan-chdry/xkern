/*
 * generic floppy driver
 * hope it works on real hardware
 */

#include "generic_floppy.hpp"
#include "dma.h"

extern "C" {
#include "io.h"
}

namespace floppy {

    bool detect_controller()
    {
        outb(DOR, 0x00); // reset controller

        // Delay before next operation.
        for (volatile int i = 0; i < 1000; ++i)
            asm volatile ("nop");

        // Enable controller with DMA.
        outb(
            DOR,
            DOR_RESET |
            DOR_DMA_ENABLE |
            DOR_DRIVE0_ENABLE
        );

        // Wait for controller ready.
        for (int i = 0; i < 100000; ++i) {
            u8 status = inb(MSR);

            if (status & MSR_RQM)
                return true;
        }

        return false;
    }


    Disk detect_disk(u8 drive)
    {
        if (drive > 3)
            return Disk::None;

        u8 dor =
            DOR_RESET |
            DOR_DMA_ENABLE |
            (drive & 0x03);

        // Enable motor for selected drive.
        dor |= static_cast<u8>(
            DOR_MOTOR0 << drive
        );

        outb(DOR, dor);

        // Give the motor time to spin up.
        for (volatile int i = 0; i < 1000000; ++i)
            asm volatile ("nop");

        // Check disk-change status.
        u8 dir = inb(DIR);

        if (dir & DIR_DISK_CHANGED)
            return Disk::Changed;

        return Disk::Present;
    }

} // namespace floppy
