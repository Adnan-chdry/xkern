#ifndef PIT_H
#define PIT_H

#include <stdint.h>

#define PIT_CH0       0x40
#define PIT_CMD       0x43
#define PIT_BASE_FREQ 1193182

void pit_init(uint32_t hz);
void pit_set_freq(uint32_t hz);
uint32_t pit_get_ticks(void);
uint32_t pit_ms_to_ticks(uint32_t ms);
void pit_sleep(uint32_t ms);
void pit_register_irq(void);
void pit_handler_irq0(void);

#endif
