/**
 * lv_console.c - LVGL-based kernel console for XKERN.
 *
 * A single full-screen lv_label acts as a terminal.  printf() feeds
 * characters into a history window; lv_console_pump() pushes the window
 * into the label and scrolls to the bottom.
 *
 * Scrolling: while output is actively streaming the scroll animates
 * (LV_ANIM_ON) for smooth motion.  Once output goes idle (>200 ms
 * without a character) the scroll snaps (LV_ANIM_OFF) so the very end
 * of the log is always fully visible - important because LVGL
 * animations only advance while lv_timer_handler() runs.
 *
 * The PIT tick handler also calls lv_console_pump(): it is throttled,
 * reentrancy-safe (print path vs IRQ) and becomes a no-op once the log
 * has settled, so idle ticks cost nothing.
 *
 * If the framebuffer is missing, lv_console_start() is never called and
 * lv_console_active() stays 0 - printf falls back to the VGA console.
 */
#include "lvgl.h"
#include "gpukit/lv_port.h"
#include "gpukit/lv_console.h"
#include "tsc.h"

/* history window kept in the label (chars) */
#define CON_KEEP    16384
/* min ms between pumps */
#define CON_PUMP_MS 30
/* ms of print silence after which we snap-scroll instead of animating */
#define CON_IDLE_MS 200

static char     con_buf[CON_KEEP + 1];  /* history ('\0'-terminated) */
static u32      con_len;
static u64      total_chars;            /* monotonically increases forever */
static int      con_active;
static u64      last_char_ms;
static u64      last_pump_ms;
static volatile int con_pumping;        /* reentrancy guard (print vs IRQ) */

/* state from the previous render cycle */
static u64      rendered_total;
static s32      cached_target;
static int      anim_pending;

static lv_obj_t *con_label;

static void pump_settle(u32 max_ms)
{
    u64 start = tsc_ms();

    /*
     * Advance LVGL timers until the scroll animation reaches the target
     * (bounded by max_ms).  This makes every pump self-contained: the
     * glide always completes even if no timer ever runs again.
     */
    for (;;) {
        lv_timer_handler();

        if ((s32)lv_obj_get_scroll_y(lv_scr_act()) == cached_target)
            break;
        if ((u64)(tsc_ms() - start) >= max_ms)
            break;
    }
}

int lv_console_active(void)
{
    return con_active;
}

int lv_console_start(u32 width, u32 height)
{
    lv_obj_t *scr;

    if (con_active)
        return 1;

    lv_port_init(width, height);

    scr = lv_scr_act();
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_set_scrollbar_mode(scr, LV_SCROLLBAR_MODE_OFF);

    con_label = lv_label_create(scr);
    lv_label_set_long_mode(con_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_pos(con_label, 8, 12);
    lv_obj_set_width(con_label, (lv_coord_t)width - 16);
    lv_label_set_text(con_label, "");
    lv_obj_set_style_text_color(con_label, lv_color_hex(0xA0A0A0), 0);
    /* 8px monospace bitmap font: maximum rows/columns for log output */
    lv_obj_set_style_text_font(con_label, &lv_font_unscii_8, 0);

    con_len = 0;
    total_chars = 0;
    rendered_total = 0;
    cached_target = 0;
    anim_pending = 0;
    last_char_ms = 0;
    con_active = 1;
    return 1;
}

/*
 * Drop the oldest lines (up to and including the first newline past the
 * midpoint) so new text always has room and the window starts on a
 * clean line boundary.
 */
static void evict_old_lines(void)
{
    u32 i = CON_KEEP / 2;

    while (i < con_len && con_buf[i] != '\n')
        i++;
    if (i >= con_len)
        return;
    i++;                                /* drop through the newline */

    for (u32 k = i; k < con_len; k++)
        con_buf[k - i] = con_buf[k];
    con_len -= i;
}

void lv_console_putc(char c)
{
    if (!con_active)
        return;

    if (con_len + 2 > CON_KEEP)
        evict_old_lines();

    /* pathological no-newline overflow: force out one oldest byte */
    if (con_len + 2 > CON_KEEP && con_len > 0) {
        for (u32 k = 1; k < con_len; k++)
            con_buf[k - 1] = con_buf[k];
        con_len--;
    }

    con_buf[con_len++] = c;
    total_chars++;
    last_char_ms = tsc_ms();

    if ((u64)(last_char_ms - last_pump_ms) >= CON_PUMP_MS)
        lv_console_pump();
}

void lv_console_pump(void)
{
    u64 flags;
    int idle;
    s32 cur;
    int dirty;

    if (!con_active || con_len == 0)
        return;

    /* reentrancy guard: enter atomically wrt interrupts */
    asm volatile ("pushfq; popq %0; cli" : "=r"(flags));
    if (con_pumping) {
        if (flags & 0x200)
            asm volatile ("sti");
        return;
    }
    con_pumping = 1;
    if (flags & 0x200)
        asm volatile ("sti");

    dirty = (total_chars != rendered_total);

    if (dirty || anim_pending) {
        if (dirty) {
            /* new text: re-render the label and refresh the target */
            con_buf[con_len] = '\0';
            lv_label_set_text(con_label, con_buf);
            lv_obj_update_layout(con_label);

            cached_target = lv_obj_get_height(con_label) + 16
                            - (s32)LV_VER_RES;
            if (cached_target < 0)
                cached_target = 0;
        }

        cur = (s32)lv_obj_get_scroll_y(lv_scr_act());
        idle = ((u64)(tsc_ms() - last_char_ms) >= CON_IDLE_MS) ? 1 : 0;

        /*
         * Animate while output streams; snap once idle so the tail of
         * the log lands exactly on screen even if timers stop later.
         */
        lv_obj_scroll_to_y(lv_scr_act(), cached_target,
                           (idle || cur == cached_target) ? LV_ANIM_OFF
                                                          : LV_ANIM_ON);
        anim_pending = (cur != cached_target);

        if (anim_pending)
            pump_settle(120);
        else
            lv_timer_handler();

        rendered_total = total_chars;
        last_pump_ms = tsc_ms();
    }

    asm volatile ("cli");
    con_pumping = 0;
    if (flags & 0x200)
        asm volatile ("sti");
}

/*
 * Force the console fully to the bottom, synchronously.
 * Call when execution is about to stop for good (panic/halt): it
 * completes any pending glide so nothing stays cropped off-screen.
 */
void lv_console_stop(void)
{
    if (!con_active)
        return;
    if (con_label) {
        lv_obj_del(con_label);
        con_label = NULL;
    }
    con_active = 0;
}

void lv_console_settle(void)
{
    u64 flags;

    if (!con_active || con_len == 0)
        return;

    asm volatile ("pushfq; popq %0; cli" : "=r"(flags));
    if (con_pumping) {
        if (flags & 0x200)
            asm volatile ("sti");
        return;
    }
    con_pumping = 1;
    if (flags & 0x200)
        asm volatile ("sti");

    /* re-render in case bytes arrived after the last pump */
    con_buf[con_len] = '\0';
    lv_label_set_text(con_label, con_buf);
    lv_obj_update_layout(con_label);

    cached_target = lv_obj_get_height(con_label) + 16 - (s32)LV_VER_RES;
    if (cached_target < 0)
        cached_target = 0;

    lv_obj_scroll_to_y(lv_scr_act(), cached_target, LV_ANIM_OFF);
    pump_settle(200);

    rendered_total = total_chars;
    anim_pending = 0;

    asm volatile ("cli");
    con_pumping = 0;
    if (flags & 0x200)
        asm volatile ("sti");
}

/*future task when added local networking
    start console at port 0
    so the kernel init can get the port
    via connect_port(MO,"0");
*/
