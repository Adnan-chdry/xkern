/*
 * engine.c - fixed-rate game loop for the xkern game engine.
 *
 * Timing comes from the TSC (tsc_ms()) rather than PIT IRQ ticks.  The
 * engine runs in the kernel boot path before the scheduler exists, so
 * PIT IRQ0 would call sched_switch() and panic (see drivers/isr.asm
 * irq0_handler).  TSC timing is monotonic, needs no interrupts, and is
 * already calibrated by tsc_init() before the engine starts.
 *
 * The loop is: poll input, run one update with the elapsed dt, render a
 * frame, flush the back buffer, then pace to GAME_FPS.
 */
#include "engine.h"
#include "gfx.h"
#include "input.h"
#include "tsc.h"
#include "klog.h"

static u32 g_frame;
static u32 g_ms_origin;
static struct game *g_current;

void engine_init(const char *title)
{
    gfx_init();

    g_frame = 0;
    g_ms_origin = (u32)tsc_ms();
    g_current = 0;

    if (title) {
        gfx_clear(GFX_BLACK);
        gfx_text((s32)((gfx_width() - gfx_text_width(title)) / 2),
                 (s32)(gfx_height() / 2 - 4), title, GFX_WHITE, GFX_BLACK);
        gfx_flush();
        engine_sleep(400);
    }
}

void engine_quit(void)
{
    if (g_current)
        g_current->quit = 1;
}

u32 engine_ms(void)
{
    return (u32)tsc_ms() - g_ms_origin;
}

u32 engine_frame(void)
{
    return g_frame;
}

void engine_sleep(u32 ms)
{
    u64 end = tsc_ms() + (u64)ms;

    while (tsc_ms() < end)
        asm volatile ("pause");
}

void engine_run(struct game *g)
{
    u32 start;
    u32 prev;

    if (!g || !g->update || !g->render)
        return;

    g->quit = 0;
    g_current = g;
    start = (u32)tsc_ms();
    prev = start;

    if (g->init)
        g->init();

    while (!g->quit) {
        s32 delay;
        u32 target;
        u32 now;

        input_poll();

        now = (u32)tsc_ms();

        if (g->run_ms && (now - start) >= g->run_ms) {
            g->quit = 1;
            break;
        }

        g->update(now - prev);
        prev = now;
        g->render();
        gfx_flush();

        if (input_pressed(GKEY_ESC))
            break;

        g_frame++;

        target = g_frame * GAME_TICK_MS;
        delay = (s32)target - (s32)((u32)tsc_ms() - start);
        if (delay > 0)
            engine_sleep((u32)delay);
    }

    g_current = 0;
    gfx_clear(GFX_BLACK);
    gfx_flush();

    klog("game.engine", "ran %u frames in %u ms", g_frame,
         (u32)tsc_ms() - start);
}
