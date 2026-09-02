/**
 * lv_port.c - LVGL port for the XKERN kernel.
 *
 * Display: flush LVGL draw buffer rows into the kernel's framebuffer via
 *          framebuffer_putpixels() / framebuffer_flush().
 * Input:   PS/2 mouse  → LV_INDEV_TYPE_POINTER  (cursor position + left btn)
 *          AT keyboard → LV_INDEV_TYPE_KEYPAD    (arrow keys, enter, esc, etc.)
 * Tick:    LV_TICK_CUSTOM reads tsc_ms() directly.
 */

#include "lvgl.h"
#include "lv_port.h"
#include "IOGraphicsFamily/fb.h"
#include "input.h"
#include "tsc.h"
#include "types.h"

/* ------------------------------------------------------------------ */
/* screen geometry (set once by lv_port_init)                          */
/* ------------------------------------------------------------------ */
static u32 g_w, g_h;

/* ------------------------------------------------------------------ */
/* draw buffer  (static, kept in BSS)                                  */
/* Sized for the largest mode we support outright (2560x1600).         */
/* Larger displays fall back to partial redraw with this same buffer.  */
/* ------------------------------------------------------------------ */
#define DRAW_BUF_MAX (2560u * 1600u)
static lv_color_t g_buf1[DRAW_BUF_MAX];
static lv_disp_draw_buf_t g_draw_buf;

/* ------------------------------------------------------------------ */
/* mouse state (updated each frame by lv_port_poll)                    */
/* ------------------------------------------------------------------ */
static s32 g_mx, g_my;
static u8  g_mbtn;

/* ------------------------------------------------------------------ */
/* keypad state                                                        */
/* ------------------------------------------------------------------ */
static uint32_t g_kbd_key;
static int      g_kbd_pressed;

/* ------------------------------------------------------------------ */
/* mouse cursor                                                        */
/* ------------------------------------------------------------------ */
#define CURSOR_W 16
#define CURSOR_H 16

/*
 * Classic arrow cursor bitmap.  Each row is exactly CURSOR_W chars.
 * 'X' = opaque black pixel, ' ' = transparent.
 * Hotspot is at (0,0) – the tip of the arrow.
 */
 const char cursor_bm[CURSOR_H][CURSOR_W] = {
    "X               ",
    "XX              ",
    "XXX             ",
    "XXXX            ",
    "XXXXX           ",
    "XXXXXXX         ",
    "XXXXXXXX        ",
    "XXXXXXXXX       ",
    "XX XXXXXXX      ",
    "XX   XXXXXX     ",
    "XX     XXXX     ",
    "XX      XXX     ",
    "          X     ",
    "           X    ",
    "            X   ",
    "                ",
};

static lv_indev_t *g_mouse_indev;
static lv_obj_t   *g_cursor_obj;
static uint8_t     g_cursor_buf[CURSOR_W * CURSOR_H * 5]; /* TRUE_COLOR_ALPHA: 4 colour + 1 alpha */

/* ================================================================== */
/* display flush                                                       */
/* ================================================================== */
static void disp_flush_cb(lv_disp_drv_t *drv, const lv_area_t *area,
                          lv_color_t *color_p)
{
    s32 y;
    s32 w = area->x2 - area->x1 + 1;

    (void)drv;
    for (y = area->y1; y <= area->y2; y++) {
        framebuffer_putpixels((u32)area->x1, (u32)y,
                              (const u32 *)color_p, (u32)w);
        color_p += w;
    }
    framebuffer_flush();
    lv_disp_flush_ready(drv);
}

/* ================================================================== */
/* pointer (mouse) indev                                               */
/* ================================================================== */
static void mouse_read_cb(lv_indev_drv_t *drv, lv_indev_data_t *data)
{
    (void)drv;
    data->point.x = g_mx;
    data->point.y = g_my;
    data->state   = g_mbtn ? LV_INDEV_STATE_PRESSED
                           : LV_INDEV_STATE_RELEASED;
}

/* ================================================================== */
/* keypad indev                                                        */
/* ================================================================== */
static void keypad_read_cb(lv_indev_drv_t *drv, lv_indev_data_t *data)
{
    (void)drv;
    if (g_kbd_pressed) {
        data->state = LV_INDEV_STATE_PRESSED;
        data->key   = g_kbd_key;
        g_kbd_pressed = 0;
    } else {
        data->state = LV_INDEV_STATE_RELEASED;
        data->key   = 0;
    }
}

/* ================================================================== */
/* build the mouse cursor image from the bitmap above                  */
/* ================================================================== */
static void build_cursor(void)
{
    lv_coord_t x, y;
    lv_color_t black = lv_color_hex(0x000000);
    lv_color_t white = lv_color_white();

    g_cursor_obj = lv_canvas_create(lv_layer_top());
    lv_canvas_set_buffer(g_cursor_obj, g_cursor_buf,
                         CURSOR_W, CURSOR_H, LV_IMG_CF_TRUE_COLOR_ALPHA);

    /* fill fully transparent */
    lv_canvas_fill_bg(g_cursor_obj, black, LV_OPA_TRANSP);

    /* macOS-style cursor: a white arrow with a thin black outline.
     * Pass 1 - white fill on the arrow pixels. */
    for (y = 0; y < CURSOR_H; y++) {
        for (x = 0; x < CURSOR_W; x++) {
            if (cursor_bm[y][x] == 'X') {
                lv_canvas_set_px_color(g_cursor_obj, x, y, white);
                lv_canvas_set_px_opa(g_cursor_obj, x, y, LV_OPA_COVER);
            }
        }
    }

    /* Pass 2 - 1px black outline on every transparent pixel that touches
     * the arrow, giving the classic white-on-dark macOS pointer. */
    for (y = 0; y < CURSOR_H; y++) {
        for (x = 0; x < CURSOR_W; x++) {
            int dx, dy, hit = 0;
            if (cursor_bm[y][x] == 'X')
                continue;
            for (dy = -1; dy <= 1 && !hit; dy++) {
                for (dx = -1; dx <= 1; dx++) {
                    lv_coord_t nx = x + dx, ny = y + dy;
                    if (nx < 0 || ny < 0 || nx >= CURSOR_W || ny >= CURSOR_H)
                        continue;
                    if (cursor_bm[ny][nx] == 'X') { hit = 1; break; }
                }
            }
            if (hit) {
                lv_canvas_set_px_color(g_cursor_obj, x, y, black);
                lv_canvas_set_px_opa(g_cursor_obj, x, y, LV_OPA_COVER);
            }
        }
    }

    /* cursor should not eat mouse events */
    lv_obj_clear_flag(g_cursor_obj, LV_OBJ_FLAG_CLICKABLE);

    /* tell the pointer indev to render this object as the cursor */
    if (g_mouse_indev)
        lv_indev_set_cursor(g_mouse_indev, g_cursor_obj);
}

/* ================================================================== */
/* public API                                                          */
/* ================================================================== */
void lv_port_init(u32 width, u32 height)
{
    static lv_disp_drv_t  disp_drv;
    static lv_indev_drv_t mouse_drv;
    static lv_indev_drv_t kbd_drv;
    static int            port_initialized;
    uint32_t need_px  = (uint32_t)width * (uint32_t)height;
    uint32_t buf_px   = need_px <= DRAW_BUF_MAX ? need_px : DRAW_BUF_MAX;

    if (port_initialized)
        return;
    port_initialized = 1;

    g_w   = width;
    g_h   = height;
    g_mx  = (s32)(width  / 2);
    g_my  = (s32)(height / 2);
    g_mbtn = 0;
    g_kbd_pressed = 0;

    lv_init();

    /* draw buffer: full-screen single-buffered when it fits, partial
     * redraw otherwise (mode larger than the reserved draw buffer) */
    lv_disp_draw_buf_init(&g_draw_buf, g_buf1, NULL, buf_px);

    /* display driver */
    lv_disp_drv_init(&disp_drv);
    disp_drv.hor_res      = (lv_coord_t)width;
    disp_drv.ver_res      = (lv_coord_t)height;
    disp_drv.flush_cb     = disp_flush_cb;
    disp_drv.draw_buf     = &g_draw_buf;
    disp_drv.full_refresh = (buf_px == need_px);
    lv_disp_drv_register(&disp_drv);

    /* pointer indev (mouse) */
    lv_indev_drv_init(&mouse_drv);
    mouse_drv.type    = LV_INDEV_TYPE_POINTER;
    mouse_drv.read_cb = mouse_read_cb;
    g_mouse_indev = lv_indev_drv_register(&mouse_drv);

    /* keypad indev (keyboard) */
    lv_indev_drv_init(&kbd_drv);
    kbd_drv.type    = LV_INDEV_TYPE_KEYPAD;
    kbd_drv.read_cb = keypad_read_cb;
    lv_indev_drv_register(&kbd_drv);

    /* don't build the mouse cursor */
    //build_cursor();
}

/* Called once per frame, right before lv_timer_handler(). */
void lv_port_poll(void)
{
    struct input_mouse m;
    u32 i;

    input_poll();

    /* --- mouse ---------------------------------------------------- */
    if (input_mouse(&m)) {
        g_mx += m.dx;
        g_my += m.dy;
        if (g_mx < 0)            g_mx = 0;
        if (g_my < 0)            g_my = 0;
        if (g_mx >= (s32)g_w)    g_mx = (s32)g_w - 1;
        if (g_my >= (s32)g_h)    g_my = (s32)g_h - 1;
        g_mbtn = m.buttons & 1;
    }

    /* --- keyboard ------------------------------------------------- */
    g_kbd_pressed = 0;

    struct { u8 sc; uint32_t lv; } map[] = {
        { 0x48, LV_KEY_UP    },
        { 0x50, LV_KEY_DOWN  },
        { 0x4B, LV_KEY_LEFT  },
        { 0x4D, LV_KEY_RIGHT },
        { 0x1C, LV_KEY_ENTER },
        { 0x01, LV_KEY_ESC   },
    };
    for (i = 0; i < sizeof(map) / sizeof(map[0]); i++) {
        if (input_pressed(map[i].sc)) {
            g_kbd_key      = map[i].lv;
            g_kbd_pressed  = 1;
            break;
        }
    }
}
