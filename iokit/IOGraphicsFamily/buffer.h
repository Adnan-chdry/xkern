#ifndef FRAMEBUFFER_BUFFER_H
#define FRAMEBUFFER_BUFFER_H

#include <stdint.h>

int framebuffer_buffer_init(uint32_t pitch, uint32_t height);
void framebuffer_buffer_free(void);

uint8_t *framebuffer_buffer_back(void);
uint32_t framebuffer_buffer_rows(void);
int framebuffer_buffer_ready(void);

void framebuffer_buffer_free_past(uint32_t *top_row, uint32_t visible_rows,
                                  uint32_t pending_lines);

#endif
