#ifndef FRAMEBUFFER_H
#define FRAMEBUFFER_H

#include <stdint.h>

struct framebuffer {
    uint8_t *address;

    uint32_t width;
    uint32_t height;
    uint32_t pitch;
    uint8_t bpp;

    uint8_t red_mask_size;
    uint8_t red_mask_shift;

    uint8_t green_mask_size;
    uint8_t green_mask_shift;

    uint8_t blue_mask_size;
    uint8_t blue_mask_shift;
};

void framebuffer_init(
    uint64_t address,
    uint32_t width,
    uint32_t height,
    uint32_t pitch,
    uint8_t bpp,

    uint8_t red_mask_size,
    uint8_t red_mask_shift,

    uint8_t green_mask_size,
    uint8_t green_mask_shift,

    uint8_t blue_mask_size,
    uint8_t blue_mask_shift
);

void framebuffer_putpixel(
    uint32_t x,
    uint32_t y,
    uint32_t color
);

void framebuffer_putpixels(
    uint32_t x,
    uint32_t y,
    const uint32_t *pixels,
    uint32_t count
);

void framebuffer_clear(uint32_t color);

void framebuffer_fill_rect(
    uint32_t x,
    uint32_t y,
    uint32_t width,
    uint32_t height,
    uint32_t color
);

void framebuffer_flush(void);

void framebuffer_scroll_up(uint32_t lines);

void framebuffer_rect(
    uint32_t x,
    uint32_t y,
    uint32_t width,
    uint32_t height,
    uint32_t color
);

int framebuffer_ready(void);
uint32_t framebuffer_width(void);
uint32_t framebuffer_height(void);

void fb_print(const char *s);

#endif