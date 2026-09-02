#include "buffer.h"
#include "fb.h"
#include "pmm.h"
#include "paging.h"
#include "string.h"
#include "klibc.h"

#define FRAMEBUFFER_BACK_SCREENS 2

static uint8_t *back_buffer;
static uint32_t back_pitch;
static uint32_t back_rows;
static uint32_t back_size;
static int back_ready;

int framebuffer_buffer_init(uint32_t pitch, uint32_t height)
{
    uint32_t size;
    uint32_t pages;
    uint32_t first;
    uint32_t prev;

    if (back_ready)
        return 1;

    if (!pitch || !height)
        return 0;

    back_pitch = pitch;
    back_rows = height * FRAMEBUFFER_BACK_SCREENS;
    size = back_pitch * back_rows;
    pages = (size + PAGE_SIZE - 1) / PAGE_SIZE;

    first = pmm_alloc();
    if (!first)
        return 0;

    prev = first;
    for (uint32_t i = 1; i < pages; i++) {
        uint32_t next = pmm_alloc();
        if (!next || next != prev - PAGE_SIZE) {
            return 0;
        }
        prev = next;
    }

    uint32_t base = first - (pages - 1) * PAGE_SIZE;
    uint32_t total = pages * PAGE_SIZE;

    paging_map_region(base, base, total, PAGE_WRITE);

    back_buffer = (uint8_t *)base;
    back_size = total;
    back_ready = 1;

    klibc.memset(back_buffer, 0x00, back_size);
    return 1;
}

void framebuffer_buffer_free(void)
{
    if (!back_ready)
        return;

    uint32_t pages = (back_size + PAGE_SIZE - 1) / PAGE_SIZE;
    uint32_t addr = (uint32_t)back_buffer;

    for (uint32_t i = 0; i < pages; i++)
        pmm_free(addr + i * PAGE_SIZE);

    back_buffer = 0;
    back_pitch = 0;
    back_rows = 0;
    back_size = 0;
    back_ready = 0;
}

uint8_t *framebuffer_buffer_back(void)
{
    return back_buffer;
}

uint32_t framebuffer_buffer_rows(void)
{
    return back_rows;
}

int framebuffer_buffer_ready(void)
{
    return back_ready;
}

/*
 * Called before the visible window is scrolled down by `pending_lines`.
 * If the window would then extend past the end of the buffer, compacts the
 * current visible window back to the top of the buffer so the scrolled-off
 * (past) rows are freed for reuse, then resets the window start.
 */
void framebuffer_buffer_free_past(uint32_t *top_row, uint32_t visible_rows,
                                  uint32_t pending_lines)
{
    if (!back_ready || !top_row || visible_rows == 0)
        return;

    if (*top_row + pending_lines + visible_rows <= back_rows)
        return;

    klibc.memmove(back_buffer, back_buffer + (uintptr_t)(*top_row) * back_pitch,
            (uintptr_t)visible_rows * back_pitch);
    klibc.memset(back_buffer + (uintptr_t)visible_rows * back_pitch, 0x00,
           (uintptr_t)(back_rows - visible_rows) * back_pitch);

    *top_row = 0;
}
