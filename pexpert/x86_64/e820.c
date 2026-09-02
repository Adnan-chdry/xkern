#include "e820.h"
#include "vga.h"
//#include "logger.h" ;not needed
#include "klog.h"

struct e820_entry e820_map[E820_MAX_ENTRIES];
uint32_t e820_count;


static void print_hex64(uint64_t value)
{
    char hex[] = "0123456789abcdef"; //replace with capslock

    for (int i = 60; i >= 0; i -= 4) {
        vga_putchar(hex[(value >> i) & 0xF]);
    }
}

#include <klibc.h>

void e820_print(void)
{
    klog("e820", "e820_print()");
    klog("e820", "E820 Memory Map:");

    for (uint32_t i = 0; i < e820_count; i++) {
        const char *type_str;

        switch (e820_map[i].type) {
            case 1:
                type_str = "usable";
                break;
            case 2:
                type_str = "reserved";
                break;
            case 3:
                type_str = "acpi";
                break;
            case 4:
                type_str = "nvs";
                break;
            default:
                type_str = "unknown";
                break;
        }

        klibc.printf(
            "base: <0x%016llx> len: <0x%016llx> type: %s\n",
            (unsigned long long)e820_map[i].base,
            (unsigned long long)e820_map[i].length,
            type_str
        );
    }
}

void mem_e820_main(struct e820_entry *map, uint32_t count)
{
    if (count > E820_MAX_ENTRIES)
        count = E820_MAX_ENTRIES;

    e820_count = count;

    for (uint32_t i = 0; i < count; i++)
        e820_map[i] = map[i];

    e820_print();
}