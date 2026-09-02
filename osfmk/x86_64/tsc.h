#ifndef TSC_H
#define TSC_H

#include <stdint.h>

void tsc_init(void);
void tsc_calibrate(void);
void tsc_disable(void);
uint64_t tsc_rdtsc(void);
uint32_t tsc_mhz(void);
uint32_t tsc_ghz(void);
uint64_t tsc_us(void);
uint64_t tsc_ms(void);
uint64_t tsc_ns(void);
uint64_t tsc_task_begin(void);
void     tsc_task_end(uint64_t start, const char *driver, const char *task);

#endif
