#pragma once

/*
 * pci config library for xkern development
 * pre 27.1.1 aug 30 2026
 */

#include "types.h"

namespace pci {

    // -------------------------------------------------------------------------
    // PCI configuration-space offsets
    // -------------------------------------------------------------------------

    constexpr u8 VENDOR_ID       = 0x00;
    constexpr u8 DEVICE_ID       = 0x02;
    constexpr u8 COMMAND         = 0x04;
    constexpr u8 STATUS          = 0x06;
    constexpr u8 REVISION_ID     = 0x08;
    constexpr u8 PROG_IF         = 0x09;
    constexpr u8 SUBCLASS        = 0x0A;
    constexpr u8 CLASS_CODE      = 0x0B;
    constexpr u8 HEADER_TYPE     = 0x0E;

    constexpr u8 BAR0            = 0x10;
    constexpr u8 BAR1            = 0x14;
    constexpr u8 BAR2            = 0x18;
    constexpr u8 BAR3            = 0x1C;
    constexpr u8 BAR4            = 0x20;
    constexpr u8 BAR5            = 0x24;

    constexpr u8 INTERRUPT_LINE  = 0x3C;
    constexpr u8 INTERRUPT_PIN   = 0x3D;

    // -------------------------------------------------------------------------
    // PCI class codes
    // -------------------------------------------------------------------------

    constexpr u8 CLASS_UNCLASSIFIED       = 0x00;
    constexpr u8 CLASS_MASS_STORAGE       = 0x01;
    constexpr u8 CLASS_NETWORK            = 0x02;
    constexpr u8 CLASS_DISPLAY            = 0x03;
    constexpr u8 CLASS_MULTIMEDIA         = 0x04;
    constexpr u8 CLASS_MEMORY              = 0x05;
    constexpr u8 CLASS_BRIDGE              = 0x06;
    constexpr u8 CLASS_SIMPLE_COMM         = 0x07;
    constexpr u8 CLASS_BASE_SYSTEM         = 0x08;
    constexpr u8 CLASS_INPUT               = 0x09;
    constexpr u8 CLASS_DOCKING             = 0x0A;
    constexpr u8 CLASS_PROCESSOR           = 0x0B;
    constexpr u8 CLASS_SERIAL_BUS          = 0x0C;

    // -------------------------------------------------------------------------
    // Common subclasses
    // -------------------------------------------------------------------------

    // Mass storage
    constexpr u8 SUBCLASS_SCSI             = 0x00;
    constexpr u8 SUBCLASS_IDE              = 0x01;
    constexpr u8 SUBCLASS_FLOPPY           = 0x02;
    constexpr u8 SUBCLASS_IPI              = 0x03;
    constexpr u8 SUBCLASS_RAID             = 0x04;
    constexpr u8 SUBCLASS_ATA              = 0x05;
    constexpr u8 SUBCLASS_SATA             = 0x06;
    constexpr u8 SUBCLASS_SAS              = 0x07;
    constexpr u8 SUBCLASS_NVME             = 0x08;

    // Serial bus
    constexpr u8 SUBCLASS_USB              = 0x03;

    // -------------------------------------------------------------------------
    // PCI command register bits
    // -------------------------------------------------------------------------

    constexpr u16 COMMAND_IO_SPACE          = 1 << 0;
    constexpr u16 COMMAND_MEMORY_SPACE      = 1 << 1;
    constexpr u16 COMMAND_BUS_MASTER        = 1 << 2;
    constexpr u16 COMMAND_SPECIAL_CYCLES    = 1 << 3;
    constexpr u16 COMMAND_MEMORY_WRITE_INV  = 1 << 4;
    constexpr u16 COMMAND_VGA_PALETTE       = 1 << 5;
    constexpr u16 COMMAND_PARITY_ERROR      = 1 << 6;
    constexpr u16 COMMAND_SERR              = 1 << 8;
    constexpr u16 COMMAND_FAST_BACK_TO_BACK  = 1 << 9;
    constexpr u16 COMMAND_INT_DISABLE       = 1 << 10;

    // -------------------------------------------------------------------------
    // PCI header types
    // -------------------------------------------------------------------------

    constexpr u8 HEADER_TYPE_NORMAL         = 0x00;
    constexpr u8 HEADER_TYPE_BRIDGE         = 0x01;
    constexpr u8 HEADER_TYPE_CARDBUS       = 0x02;

    // -------------------------------------------------------------------------
    // PCI BAR types
    // -------------------------------------------------------------------------

    enum class BarType {
        Memory,
        IO
    };

    struct Bar {
        u64 address;
        u64 size;
        BarType type;
        bool is_64bit;
        bool prefetchable;
    };

    // -------------------------------------------------------------------------
    // PCI configuration access
    //
    // Implement these in your architecture-specific PCI code.
    // For x86, this normally uses CONFIG_ADDRESS (0xCF8)
    // and CONFIG_DATA    (0xCFC).
    // -------------------------------------------------------------------------

    extern "C" {

        u32 config_read(
            u8 bus,
            u8 device,
            u8 function,
            u8 offset
        );

        void config_write(
            u8 bus,
            u8 device,
            u8 function,
            u8 offset,
            u32 value
        );
    }

    // -------------------------------------------------------------------------
    // Basic register access
    // -------------------------------------------------------------------------

    inline u16 read16(u8 bus, u8 device, u8 function, u8 offset)
    {
        u32 value = config_read(bus, device, function, offset & 0xFC);

        return static_cast<u16>(
            (value >> ((offset & 2) * 8)) & 0xFFFF
        );
    }

    inline u32 read32(u8 bus, u8 device, u8 function, u8 offset)
    {
        return config_read(
            bus,
            device,
            function,
            offset & 0xFC
        );
    }

    inline void write16(
        u8 bus,
        u8 device,
        u8 function,
        u8 offset,
        u16 value
    )
    {
        const u8 aligned = offset & 0xFC;
        const u32 shift = (offset & 2) * 8;

        u32 old = config_read(
            bus,
            device,
            function,
            aligned
        );

        old &= ~(0xFFFFu << shift);
        old |= static_cast<u32>(value) << shift;

        config_write(
            bus,
            device,
            function,
            aligned,
            old
        );
    }

    inline void write32(
        u8 bus,
        u8 device,
        u8 function,
        u8 offset,
        u32 value
    )
    {
        config_write(
            bus,
            device,
            function,
            offset & 0xFC,
            value
        );
    }

    // -------------------------------------------------------------------------
    // Device identification
    // -------------------------------------------------------------------------

    inline u16 vendor_id(
        u8 bus,
        u8 device,
        u8 function
    )
    {
        return read16(bus, device, function, VENDOR_ID);
    }

    inline u16 device_id(
        u8 bus,
        u8 device,
        u8 function
    )
    {
        return read16(bus, device, function, DEVICE_ID);
    }

    inline u8 class_code(
        u8 bus,
        u8 device,
        u8 function
    )
    {
        return static_cast<u8>(
            read32(bus, device, function, CLASS_CODE) >> 24
        );
    }

    inline u8 subclass(
        u8 bus,
        u8 device,
        u8 function
    )
    {
        return static_cast<u8>(
            (read32(bus, device, function, CLASS_CODE) >> 16) & 0xFF
        );
    }

    inline u8 prog_if(
        u8 bus,
        u8 device,
        u8 function
    )
    {
        return static_cast<u8>(
            (read32(bus, device, function, CLASS_CODE) >> 8) & 0xFF
        );
    }

    inline u8 header_type(
        u8 bus,
        u8 device,
        u8 function
    )
    {
        return static_cast<u8>(
            (read32(bus, device, function, HEADER_TYPE) >> 16) & 0xFF
        );
    }

    // -------------------------------------------------------------------------
    // PCI command register helpers
    // -------------------------------------------------------------------------

    inline u16 command(
        u8 bus,
        u8 device,
        u8 function
    )
    {
        return read16(bus, device, function, COMMAND);
    }

    inline void set_command_bits(
        u8 bus,
        u8 device,
        u8 function,
        u16 bits
    )
    {
        u16 value = command(bus, device, function);
        value |= bits;

        write16(
            bus,
            device,
            function,
            COMMAND,
            value
        );
    }

    inline void clear_command_bits(
        u8 bus,
        u8 device,
        u8 function,
        u16 bits
    )
    {
        u16 value = command(bus, device, function);
        value &= ~bits;

        write16(
            bus,
            device,
            function,
            COMMAND,
            value
        );
    }

    inline void enable_memory_space(
        u8 bus,
        u8 device,
        u8 function
    )
    {
        set_command_bits(
            bus,
            device,
            function,
            COMMAND_MEMORY_SPACE
        );
    }

    inline void enable_io_space(
        u8 bus,
        u8 device,
        u8 function
    )
    {
        set_command_bits(
            bus,
            device,
            function,
            COMMAND_IO_SPACE
        );
    }

    inline void enable_bus_master(
        u8 bus,
        u8 device,
        u8 function
    )
    {
        set_command_bits(
            bus,
            device,
            function,
            COMMAND_BUS_MASTER
        );
    }

    // -------------------------------------------------------------------------
    // Device discovery
    // -------------------------------------------------------------------------

    inline bool find_class(
        u8 wanted_class,
        u8 wanted_subclass,
        u8 wanted_progif,
        u8* out_bus,
        u8* out_device,
        u8* out_function
    )
    {
        for (u16 bus = 0; bus < 256; ++bus) {
            for (u8 device = 0; device < 32; ++device) {
                for (u8 function = 0; function < 8; ++function) {

                    const u16 vendor =
                        vendor_id(
                            static_cast<u8>(bus),
                            device,
                            function
                        );

                    if (vendor == 0xFFFF)
                        continue;

                    if (class_code(
                            static_cast<u8>(bus),
                            device,
                            function) != wanted_class)
                        continue;

                    if (subclass(
                            static_cast<u8>(bus),
                            device,
                            function) != wanted_subclass)
                        continue;

                    if (wanted_progif != 0xFF &&
                        prog_if(
                            static_cast<u8>(bus),
                            device,
                            function) != wanted_progif)
                        continue;

                    if (out_bus)
                        *out_bus = static_cast<u8>(bus);

                    if (out_device)
                        *out_device = device;

                    if (out_function)
                        *out_function = function;

                    return true;
                }
            }
        }

        return false;
    }

    // -------------------------------------------------------------------------
    // BAR access
    // -------------------------------------------------------------------------

    inline u32 read_bar(
        u8 bus,
        u8 device,
        u8 function,
        u8 bar
    )
    {
        return read32(
            bus,
            device,
            function,
            BAR0 + bar * 4
        );
    }

    inline BarType bar_type(u32 bar)
    {
        return (bar & 1)
            ? BarType::IO
            : BarType::Memory;
    }

    inline bool bar_is_64bit(u32 bar)
    {
        if (bar & 1)
            return false;

        return ((bar >> 1) & 0x3) == 0x2;
    }

    inline bool bar_is_prefetchable(u32 bar)
    {
        if (bar & 1)
            return false;

        return (bar & 0x8) != 0;
    }

    inline u64 bar_address(
        u8 bus,
        u8 device,
        u8 function,
        u8 bar
    )
    {
        u32 low = read_bar(
            bus,
            device,
            function,
            bar
        );

        if (low & 1) {
            // I/O BAR
            return static_cast<u64>(low & ~0x3u);
        }

        // 64-bit memory BAR
        if (((low >> 1) & 0x3) == 0x2) {
            u32 high = read_bar(
                bus,
                device,
                function,
                bar + 1
            );

            return (static_cast<u64>(high) << 32) |
                   static_cast<u64>(low & ~0xFu);
        }

        // 32-bit memory BAR
        return static_cast<u64>(low & ~0xFu);
    }

} // namespace pci
