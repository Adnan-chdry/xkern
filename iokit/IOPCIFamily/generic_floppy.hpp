#pragma once

#include "types.h"

namespace floppy {

    // Primary floppy controller I/O ports.
    constexpr u16 DOR  = 0x3F2; // Digital Output Register
    constexpr u16 MSR  = 0x3F4; // Main Status Register
    constexpr u16 FIFO = 0x3F5; // Data FIFO
    constexpr u16 DIR  = 0x3F7; // Digital Input Register

    // Digital Output Register bits.
    constexpr u8 DOR_DRIVE0_ENABLE = 1 << 0;
    constexpr u8 DOR_DRIVE1_ENABLE = 1 << 1;
    constexpr u8 DOR_RESET         = 1 << 2;
    constexpr u8 DOR_DMA_ENABLE    = 1 << 3;
    constexpr u8 DOR_MOTOR0        = 1 << 4;
    constexpr u8 DOR_MOTOR1        = 1 << 5;
    constexpr u8 DOR_MOTOR2        = 1 << 6;
    constexpr u8 DOR_MOTOR3        = 1 << 7;

    // Main Status Register bits.
    constexpr u8 MSR_ACTIVE_DRIVE  = 1 << 0;
    constexpr u8 MSR_ACTIVE_DRIVE1 = 1 << 1;
    constexpr u8 MSR_ACTIVE_DRIVE2 = 1 << 2;
    constexpr u8 MSR_ACTIVE_DRIVE3 = 1 << 3;
    constexpr u8 MSR_CMDBUSY       = 1 << 4;
    constexpr u8 MSR_NON_DMA       = 1 << 5;
    constexpr u8 MSR_DIO           = 1 << 6;
    constexpr u8 MSR_RQM           = 1 << 7;

    // DIR bits.
    constexpr u8 DIR_DISK_CHANGED = 1 << 7;

    // Result of controller detection.
    enum class Controller {
        None,
        Primary
    };

    // Result of disk detection.
    enum class Disk {
        None,
        Present,
        Changed
    };

    // Detect the primary legacy floppy controller.
    bool detect_controller();

    // Check whether drive 0 appears to contain a floppy.
    Disk detect_disk(u8 drive);

} // namespace floppy
