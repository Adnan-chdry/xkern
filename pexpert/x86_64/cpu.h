#ifndef CPU_H
#define CPU_H

#include <stdint.h>

#define CPU_VENDOR_LENGTH 13
#define CPU_NAME_LENGTH 49

typedef struct {
    char vendor[CPU_VENDOR_LENGTH];
    char name[CPU_NAME_LENGTH];
} cpu_info_t;

void cpu_get_info(cpu_info_t *info);

#endif
