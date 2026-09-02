#ifndef IO_H
#define IO_H

#include "types.h"

static inline u8 inb(u16 port) {
    u8 val;
    asm volatile ("inb %1, %0" : "=a"(val) : "Nd"(port));
    return val;
}

static inline void outb(u16 port, u8 val) {
    asm volatile ("outb %0, %1" : : "a"(val), "Nd"(port));
}

static inline u16 inw(u16 port) {
    u16 val;
    asm volatile ("inw %1, %0" : "=a"(val) : "Nd"(port));
    return val;
}

static inline void outw(u16 port, u16 val) {
    asm volatile ("outw %0, %1" : : "a"(val), "Nd"(port));
}

static inline u32 inl(u16 port) {
    u32 val;
    asm volatile ("inl %1, %0" : "=a"(val) : "Nd"(port));
    return val;
}

static inline void outl(u16 port, u32 val) {
    asm volatile ("outl %0, %1" : : "a"(val), "Nd"(port));
}

#endif