#ifndef E820_H
#define E820_H

#include <stdint.h>

#define E820_MAX_ENTRIES 128

struct e820_entry {
    uint64_t base;
    uint64_t length;
    uint32_t type;
    uint32_t attr;
} __attribute__((packed));

extern struct e820_entry e820_map[E820_MAX_ENTRIES];
extern uint32_t e820_count;

void e820_print(void);
void mem_e820_main(struct e820_entry *map, uint32_t count);

#endif