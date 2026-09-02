#include "pizza.h"
#include "game.h"
#include "IOGraphicsFamily/fb.h"
#include "klibc.h"
#include "klog.h"
#include "tsc.h"

#define MAX_PIZZA 10
#define MAX_OBST  14
#define MAX_HOUSE 12

#define MAX_CARRY 3
#define MAX_LIVES 3

#define ST_TITLE 0
#define ST_PLAY  1
#define ST_OVER  2

#define OB_CONE 0
#define OB_CAR  1
#define OB_CAT  2

struct pizza {
    int   alive;
    float wy;
    int   lane;
};

struct obst {
    int   alive;
    float wy;
    int   lane;
    int   kind;
    s32   xo;
    int   dir;
    int   flee;
    int   scared;
};

struct house {
    int   alive;
    float wy;
    int   side;
    int   num;
    int   done;
};

static u32 g_w, g_h;
static s32 g_side, g_road_l, g_road_w, g_road_r, g_lane0, g_lane1;
static s32 g_pl_x, g_pl_y;
static s32 g_carry, g_lives;
static u32 g_score;
static int g_delivered;
static int g_streak;
static int g_target;
static float g_crank;
static float g_scroll;
static float g_base;
static int g_state;
static u32 g_invuln;
static u32 g_flash;
static char g_toast[48];
static s32 g_toast_ms;
static s32 g_house_t, g_pizza_t, g_obst_t;
static u32 g_rng;
static u32 g_hi;

static struct pizza g_pizzas[MAX_PIZZA];
static struct obst  g_obsts[MAX_OBST];
static struct house g_houses[MAX_HOUSE];

static const u8 s_heart[8] = { 0x6C, 0xFE, 0xFE, 0xFE, 0x7C, 0x38, 0x10, 0x00 };

static const u8 s_bayer[16] = {
    0, 8, 2, 10,
    12, 4, 14, 6,
    3, 11, 1, 9,
    15, 7, 13, 5
};

static const s32 s_cos16[16] = {
    1000, 924, 707, 383, 0, -383, -707, -924,
    -1000, -924, -707, -383, 0, 383, 707, 924
};

static u32 rng(void)
{
    g_rng ^= g_rng << 13;
    g_rng ^= g_rng >> 17;
    g_rng ^= g_rng << 5;
    return g_rng;
}

static s32 center_x(const char *s)
{
    s32 tw = (s32)gfx_text_width(s);

    return ((s32)g_w - tw) / 2;
}

static s32 lane_x(int lane)
{
    return lane ? g_lane1 : g_lane0;
}

static s32 house_w(void)
{
    s32 hw = g_side - 8;

    return hw < 22 ? 22 : hw;
}

static s32 house_door_x(const struct house *h)
{
    s32 hw = house_w();
    s32 hx = h->side ? (s32)(g_w - g_side / 2) : (s32)(g_side / 2);
    s32 x0 = hx - hw / 2;

    return h->side ? x0 : x0 + hw;
}

static int box_overlap(s32 ax, s32 ay, s32 aw, s32 ah,
                       s32 bx, s32 by, s32 bw, s32 bh)
{
    return ax < bx + bw && ax + aw > bx &&
           ay < by + bh && ay + ah > by;
}

static void toast(const char *s)
{
    s32 i;

    for (i = 0; i < 47 && s[i]; i++)
        g_toast[i] = s[i];
    g_toast[i] = 0;
    g_toast_ms = 900;
}

static void dither_rect(s32 x, s32 y, s32 w, s32 h, int th)
{
    s32 yy, xx;

    if (w <= 0 || h <= 0 || th <= 0)
        return;

    if (th >= 16) {
        gfx_rect(x, y, (u32)w, (u32)h, GFX_WHITE);
        return;
    }

    for (yy = 0; yy < h; yy++)
        for (xx = 0; xx < w; xx++)
            if (s_bayer[(((y + yy) & 3) << 2) | ((x + xx) & 3)] < th)
                gfx_pixel(x + xx, y + yy, GFX_WHITE);
}

static void spawn_pizza(void)
{
    int i;

    for (i = 0; i < MAX_PIZZA; i++) {
        struct pizza *p = &g_pizzas[i];

        if (p->alive)
            continue;
        p->alive = 1;
        p->lane = (int)(rng() % 2);
        p->wy = -40.0f - g_scroll;
        return;
    }
}

static void spawn_obst(void)
{
    int i;

    for (i = 0; i < MAX_OBST; i++) {
        struct obst *o = &g_obsts[i];

        if (o->alive)
            continue;
        o->alive = 1;
        o->lane = (int)(rng() % 2);
        o->kind = (int)(rng() % 3);
        if (g_delivered < 2 && o->kind == OB_CAR)
            o->kind = OB_CONE;
        o->wy = -50.0f - g_scroll;
        o->xo = 0;
        o->dir = (rng() & 1) ? 1 : -1;
        o->flee = 0;
        o->scared = 0;
        return;
    }
}

static void spawn_house(void)
{
    int i;

    for (i = 0; i < MAX_HOUSE; i++) {
        struct house *h = &g_houses[i];

        if (h->alive)
            continue;
        h->alive = 1;
        h->side = (int)(rng() % 2);
        h->num = 1 + (int)(rng() % 9);
        h->wy = -90.0f - g_scroll;
        h->done = 0;
        return;
    }
}

static void game_init(void)
{
    int i;

    g_w = gfx_width();
    g_h = gfx_height();

    g_side = (s32)g_w / 5;
    g_road_l = g_side;
    g_road_w = (s32)g_w - 2 * g_side;
    g_road_r = g_road_l + g_road_w;
    g_lane0 = g_road_l + g_road_w / 4;
    g_lane1 = g_road_l + 3 * g_road_w / 4;

    g_pl_x = (s32)g_w / 2;
    g_pl_y = (s32)g_h - 90;

    g_carry = 0;
    g_lives = MAX_LIVES;
    g_score = 0;
    g_delivered = 0;
    g_streak = 0;
    g_target = 1 + (int)(rng() % 9);
    g_crank = 30.0f;
    g_scroll = 0.0f;
    g_base = 60.0f;
    g_invuln = 0;
    g_flash = 0;
    g_toast_ms = 0;
    g_house_t = 1200;
    g_pizza_t = 400;
    g_obst_t = 2200;

    for (i = 0; i < MAX_PIZZA; i++)
        g_pizzas[i].alive = 0;
    for (i = 0; i < MAX_OBST; i++)
        g_obsts[i].alive = 0;
    for (i = 0; i < MAX_HOUSE; i++)
        g_houses[i].alive = 0;
}

static void draw_road(void)
{
    s32 cx = g_road_l + g_road_w / 2;
    s32 off, y;

    gfx_rect(g_road_l, 0, 2, g_h, GFX_WHITE);
    gfx_rect(g_road_r - 2, 0, 2, g_h, GFX_WHITE);

    off = (s32)((u32)g_scroll % 28u);
    for (y = off - 28; y < (s32)g_h; y += 28)
        gfx_rect(cx - 1, y, 2, 15, GFX_WHITE);

    off = (s32)((u32)g_scroll % 52u);
    for (y = off - 52; y < (s32)g_h; y += 52) {
        gfx_rect(g_road_l + 8, y, 8, 2, GFX_WHITE);
        gfx_rect(g_road_r - 16, y, 8, 2, GFX_WHITE);
    }
}

static void draw_house(const struct house *h)
{
    s32 hw = house_w();
    s32 hh = 26;
    s32 hx = h->side ? (s32)(g_w - g_side / 2) : (s32)(g_side / 2);
    s32 x0 = hx - hw / 2;
    s32 sy = (s32)(h->wy + g_scroll);
    char num[2];

    gfx_rect_outline(x0, sy - hh / 2, (u32)hw, (u32)hh, GFX_WHITE);
    dither_rect(x0 + 1, sy - hh / 2 + 1, hw - 2, hh - 2, 6);
    gfx_rect(x0 + 1, sy - hh / 2 + 1, (u32)(hw - 2), 2, GFX_WHITE);

    if (h->side) {
        gfx_rect(x0, sy - 5, 3, 10, GFX_BLACK);
        if (h->done) {
            gfx_line(x0 - 5, sy, x0 - 3, sy + 2, GFX_WHITE);
            gfx_line(x0 - 3, sy + 2, x0 + 2, sy - 3, GFX_WHITE);
        } else {
            gfx_rect(x0 - 4, sy - 2, 4, 4, GFX_WHITE);
        }
    } else {
        gfx_rect(x0 + hw - 3, sy - 5, 3, 10, GFX_BLACK);
        if (h->done) {
            gfx_line(x0 + hw + 5, sy, x0 + hw + 3, sy + 2, GFX_WHITE);
            gfx_line(x0 + hw + 3, sy + 2, x0 + hw - 2, sy - 3, GFX_WHITE);
        } else {
            gfx_rect(x0 + hw, sy - 2, 4, 4, GFX_WHITE);
        }
    }

    gfx_rect(hx - 3, sy - 1, 6, 6, GFX_WHITE);
    gfx_rect(hx - 1, sy - 1, 2, 6, GFX_BLACK);
    gfx_rect(hx - 3, sy + 1, 6, 2, GFX_BLACK);

    num[0] = (char)('0' + h->num);
    num[1] = 0;
    gfx_text(hx - 4, sy - hh / 2 - 12, num, GFX_WHITE, GFX_BLACK);
}

static void draw_pizza_box(s32 sx, s32 sy)
{
    gfx_rect(sx - 8, sy - 8, 16, 16, GFX_WHITE);
    gfx_rect(sx - 1, sy - 8, 2, 16, GFX_BLACK);
    gfx_rect(sx - 8, sy - 1, 16, 2, GFX_BLACK);
    gfx_pixel(sx - 4, sy - 4, GFX_BLACK);
    gfx_pixel(sx + 3, sy - 5, GFX_BLACK);
    gfx_pixel(sx + 2, sy + 3, GFX_BLACK);
}

static void draw_cone(s32 sx, s32 sy)
{
    gfx_triangle(sx, sy - 9, sx - 6, sy + 6, sx + 6, sy + 6, GFX_WHITE);
    gfx_rect(sx - 6, sy + 6, 12, 2, GFX_WHITE);
    gfx_rect(sx - 4, sy + 2, 8, 2, GFX_BLACK);
    gfx_rect(sx - 2, sy - 3, 4, 2, GFX_BLACK);
}

static void draw_car(s32 sx, s32 sy)
{
    gfx_rect(sx - 13, sy - 8, 26, 16, GFX_WHITE);
    gfx_rect(sx - 9, sy - 4, 18, 5, GFX_BLACK);
    gfx_rect(sx - 15, sy - 5, 2, 10, GFX_WHITE);
    gfx_rect(sx + 13, sy - 5, 2, 10, GFX_WHITE);
}

static void draw_cat(s32 sx, s32 sy, int frame, int flee)
{
    gfx_line(sx - 6, sy - 3, sx - 3, sy + 3, GFX_WHITE);
    gfx_line(sx - 6, sy - 3, sx - 8, sy - 5, GFX_WHITE);

    gfx_rect(sx - 5, sy - 1, 9, 7, GFX_WHITE);

    if (frame) {
        gfx_rect(sx - 5, sy + 6, 2, 3, GFX_WHITE);
        gfx_rect(sx + 2, sy + 6, 2, 3, GFX_WHITE);
    } else {
        gfx_rect(sx - 3, sy + 6, 2, 3, GFX_WHITE);
        gfx_rect(sx + 4, sy + 6, 2, 3, GFX_WHITE);
    }

    gfx_circle(sx + 5, sy - 2, 3, GFX_WHITE);
    gfx_triangle(sx + 5, sy - 6, sx + 2, sy - 6, sx + 4, sy - 3, GFX_WHITE);
    gfx_triangle(sx + 9, sy - 6, sx + 6, sy - 6, sx + 7, sy - 3, GFX_WHITE);
    gfx_pixel(sx + 6, sy - 2, GFX_BLACK);

    if (flee)
        gfx_text(sx + 2, sy - 15, "!", GFX_WHITE, GFX_BLACK);
}

static void draw_scooter(s32 x, s32 y, int carry)
{
    int i;

    gfx_rect(x - 4, y - 18, 8, 7, GFX_WHITE);
    gfx_rect(x - 4, y + 11, 8, 7, GFX_WHITE);
    gfx_rect(x - 6, y - 11, 12, 22, GFX_WHITE);
    gfx_rect(x - 4, y - 9, 8, 3, GFX_BLACK);
    gfx_rect(x - 8, y - 18, 16, 2, GFX_WHITE);
    gfx_pixel(x, y - 20, GFX_WHITE);

    gfx_circle(x, y + 2, 5, GFX_WHITE);
    gfx_rect(x - 3, y + 1, 6, 2, GFX_BLACK);

    for (i = 0; i < carry && i < MAX_CARRY; i++) {
        s32 by = y - 25 - i * 3;

        gfx_rect(x - 6, by, 12, 9, GFX_WHITE);
        gfx_rect(x - 1, by, 2, 9, GFX_BLACK);
        gfx_rect(x - 6, by + 3, 12, 2, GFX_BLACK);
    }
}

static void draw_crank_spin(s32 cx, s32 cy, s32 val)
{
    s32 r = 12;
    s32 idx = (s32)((float)val * 0.16f);
    s32 hx, hy;
    char buf[16];

    if (idx > 15)
        idx = 15;

    gfx_circle_outline(cx, cy, r, GFX_WHITE);
    gfx_rect(cx - 1, cy - r, 2, 3, GFX_WHITE);
    gfx_rect(cx + r - 1, cy - 1, 3, 2, GFX_WHITE);
    gfx_rect(cx - 1, cy + r - 2, 2, 3, GFX_WHITE);
    gfx_rect(cx - r - 1, cy - 1, 3, 2, GFX_WHITE);

    hx = cx + (s_cos16[idx] * r) / 1000;
    hy = cy + (s_cos16[(idx + 4) & 15] * r) / 1000;
    gfx_line(cx, cy, hx, hy, GFX_WHITE);
    gfx_circle(hx, hy, 2, GFX_WHITE);

    klibc.sprintf(buf, "%d%%", val);
    gfx_text(cx - (s32)(gfx_text_width(buf) / 2), cy + r + 5, buf, GFX_WHITE, GFX_BLACK);
    gfx_text(cx - 22, cy - r - 11, "CRANK", GFX_WHITE, GFX_BLACK);
}

static void draw_hud(void)
{
    char buf[48];
    int i;

    klibc.sprintf(buf, "SCORE %05u", g_score);
    gfx_text(6, 6, buf, GFX_WHITE, GFX_BLACK);

    for (i = 0; i < MAX_LIVES; i++) {
        u32 col = (i < g_lives) ? GFX_WHITE : GFX_RGB(0x28, 0x28, 0x28);

        gfx_sprite(6 + i * 10, 16, 8, 8, s_heart, col, GFX_BLACK, 0);
    }

    klibc.sprintf(buf, "DELIVER TO #%d", g_target);
    gfx_text(center_x(buf), 6, buf, GFX_WHITE, GFX_BLACK);

    klibc.sprintf(buf, "%d PIZZAS", g_delivered);
    gfx_text(center_x(buf), 16, buf, GFX_WHITE, GFX_BLACK);

    klibc.sprintf(buf, "PIZZA x%d", g_carry);
    gfx_text(6, 28, buf, GFX_WHITE, GFX_BLACK);

    draw_crank_spin((s32)g_w - 26, 24, (s32)g_crank);
}

static void render_world(void)
{
    int i;
    s32 frame = (s32)(engine_ms() / 110) & 1;

    gfx_clear(GFX_BLACK);
    draw_road();

    for (i = 0; i < MAX_HOUSE; i++)
        if (g_houses[i].alive)
            draw_house(&g_houses[i]);

    for (i = 0; i < MAX_PIZZA; i++) {
        struct pizza *p = &g_pizzas[i];

        if (p->alive)
            draw_pizza_box(lane_x(p->lane), (s32)(p->wy + g_scroll));
    }

    for (i = 0; i < MAX_OBST; i++) {
        struct obst *o = &g_obsts[i];
        s32 sx, sy;

        if (!o->alive)
            continue;
        sx = lane_x(o->lane) + o->xo;
        sy = (s32)(o->wy + g_scroll);

        if (o->kind == OB_CONE)
            draw_cone(sx, sy);
        else if (o->kind == OB_CAR)
            draw_car(sx, sy);
        else
            draw_cat(sx, sy, frame, o->flee > 0);
    }

    if (!(g_invuln && ((g_invuln / 100) & 1)))
        draw_scooter(g_pl_x, g_pl_y, g_carry);

    draw_hud();

    if (g_toast_ms > 0) {
        s32 tw = (s32)gfx_text_width(g_toast);
        s32 tx = ((s32)g_w - tw) / 2;

        gfx_rect(tx - 4, (s32)g_h - 42, (u32)(tw + 8), 12, GFX_BLACK);
        gfx_rect_outline(tx - 4, (s32)g_h - 42, (u32)(tw + 8), 12, GFX_WHITE);
        gfx_text(tx, (s32)g_h - 39, g_toast, GFX_WHITE, GFX_BLACK);
    }

    if (g_flash > 0)
        gfx_rect(0, 0, g_w, g_h, GFX_WHITE);
}

static void render_title(void)
{
    const char *t = "KERNEL PIZZA PATROL";
    const char *s = "A PLAYDATE-STYLE 1-BIT DELIVERY DASH";
    const char *c1 = "LEFT/RIGHT  STEER     UP/DOWN  CRANK";
    const char *c2 = "SPACE  HONK + START";
    const char *c3 = "PRESS SPACE TO START    ESC TO QUIT";
    s32 cx = (s32)(g_w / 2);
    s32 ty = (s32)(g_h / 2) - 34;
    s32 spin = (s32)((engine_ms() / 50) % 100);

    gfx_clear(GFX_BLACK);
    gfx_rect_outline(0, 0, g_w, g_h, GFX_WHITE);

    gfx_circle_outline(cx, ty - 24, 18, GFX_WHITE);
    gfx_pixel(cx - 6, ty - 28, GFX_WHITE);
    gfx_pixel(cx + 2, ty - 30, GFX_WHITE);
    gfx_pixel(cx + 6, ty - 22, GFX_WHITE);

    gfx_text(center_x(t), ty, t, GFX_WHITE, GFX_BLACK);
    gfx_rect(center_x(t), ty + 9, gfx_text_width(t), 2, GFX_WHITE);

    gfx_text(center_x(s), ty + 16, s, GFX_WHITE, GFX_BLACK);

    draw_scooter(cx, ty + 66, 2);
    draw_crank_spin(cx, ty + 104, spin);

    gfx_text(center_x(c1), ty + 132, c1, GFX_WHITE, GFX_BLACK);
    gfx_text(center_x(c2), ty + 144, c2, GFX_WHITE, GFX_BLACK);
    gfx_text(center_x(c3), ty + 156, c3, GFX_WHITE, GFX_BLACK);
}

static void render_over(void)
{
    char buf[48];
    s32 cy = (s32)(g_h / 2) - 40;

    gfx_rect(0, cy, g_w, 92, GFX_BLACK);
    gfx_rect_outline(0, cy, g_w, 92, GFX_WHITE);

    gfx_text(center_x("GAME OVER"), cy + 8, "GAME OVER", GFX_WHITE, GFX_BLACK);

    klibc.sprintf(buf, "FINAL SCORE %u", g_score);
    gfx_text(center_x(buf), cy + 22, buf, GFX_WHITE, GFX_BLACK);

    klibc.sprintf(buf, "%d PIZZAS DELIVERED", g_delivered);
    gfx_text(center_x(buf), cy + 36, buf, GFX_WHITE, GFX_BLACK);

    klibc.sprintf(buf, "BEST %u", g_hi);
    gfx_text(center_x(buf), cy + 50, buf, GFX_WHITE, GFX_BLACK);

    klibc.sprintf(buf, "PRESS ENTER TO RESTART    ESC TO QUIT", 0);
    gfx_text(center_x(buf), cy + 70, buf, GFX_WHITE, GFX_BLACK);
}

static void update_play(u32 dt_ms)
{
    float dt = (float)dt_ms / 1000.0f;
    int i;

    if (g_toast_ms > 0) {
        g_toast_ms -= (s32)dt_ms;
        if (g_toast_ms < 0)
            g_toast_ms = 0;
    }
    if (g_flash > 0) {
        if (g_flash <= dt_ms)
            g_flash = 0;
        else
            g_flash -= dt_ms;
    }
    if (g_invuln > 0) {
        if (g_invuln <= dt_ms)
            g_invuln = 0;
        else
            g_invuln -= dt_ms;
    }

    if (input_key(GKEY_UP) || input_key(GKEY_W))
        g_crank += 95.0f * dt;
    if (input_key(GKEY_DOWN) || input_key(GKEY_S))
        g_crank -= 95.0f * dt;
    if (g_crank > 100.0f)
        g_crank = 100.0f;
    if (g_crank < 0.0f)
        g_crank = 0.0f;
    if (!input_key(GKEY_UP) && !input_key(GKEY_DOWN) &&
        !input_key(GKEY_W) && !input_key(GKEY_S)) {
        if (g_crank > 30.0f)
            g_crank -= 42.0f * dt;
        else if (g_crank < 30.0f)
            g_crank += 42.0f * dt;
    }

    if (input_key(GKEY_LEFT) || input_key(GKEY_A))
        g_pl_x -= (s32)(360.0f * dt);
    if (input_key(GKEY_RIGHT) || input_key(GKEY_D))
        g_pl_x += (s32)(360.0f * dt);
    if (g_pl_x < 14)
        g_pl_x = 14;
    if (g_pl_x > (s32)g_w - 14)
        g_pl_x = (s32)g_w - 14;

    g_scroll += (g_base + g_crank * 3.4f) * dt;

    g_house_t -= (s32)dt_ms;
    if (g_house_t <= 0) {
        spawn_house();
        g_house_t = 2600 - g_delivered * 60;
        if (g_house_t < 1200)
            g_house_t = 1200;
    }

    g_pizza_t -= (s32)dt_ms;
    if (g_pizza_t <= 0) {
        if (g_carry < MAX_CARRY)
            spawn_pizza();
        g_pizza_t = 1500;
    }

    g_obst_t -= (s32)dt_ms;
    if (g_obst_t <= 0) {
        spawn_obst();
        g_obst_t = 2100 - g_delivered * 70;
        if (g_obst_t < 620)
            g_obst_t = 620;
    }

    if (input_pressed(GKEY_SPACE)) {
        int scared = 0;

        for (i = 0; i < MAX_OBST; i++) {
            struct obst *o = &g_obsts[i];
            s32 sx, sy;

            if (!o->alive || o->kind != OB_CAT || o->scared)
                continue;
            sx = lane_x(o->lane) + o->xo;
            sy = (s32)(o->wy + g_scroll);
            if (sx > g_pl_x - 110 && sx < g_pl_x + 110 &&
                sy > g_pl_y - 110 && sy < g_pl_y + 110) {
                o->scared = 1;
                o->flee = 1000;
                o->dir = (sx >= g_pl_x) ? 1 : -1;
                g_score += 10;
                scared++;
            }
        }
        if (scared)
            toast("HONK! CAT FLED +10");
        else
            toast("HONK!");
    }

    for (i = 0; i < MAX_OBST; i++) {
        struct obst *o = &g_obsts[i];

        if (!o->alive || o->kind != OB_CAT)
            continue;
        if (o->flee > 0) {
            o->flee -= (s32)dt_ms;
            o->xo += o->dir * (s32)(170.0f * dt);
        } else {
            if ((rng() & 63) == 0)
                o->dir = -o->dir;
            o->xo += o->dir * (s32)(26.0f * dt);
            if (o->xo > 26)
                o->xo = 26;
            if (o->xo < -26)
                o->xo = -26;
        }
    }

    for (i = 0; i < MAX_PIZZA; i++) {
        struct pizza *p = &g_pizzas[i];
        s32 sx, sy;

        if (!p->alive)
            continue;
        sx = lane_x(p->lane);
        sy = (s32)(p->wy + g_scroll);
        if (sy > (s32)g_h + 40) {
            p->alive = 0;
            continue;
        }
        if (box_overlap(g_pl_x - 12, g_pl_y - 16, 24, 32,
                        sx - 8, sy - 8, 16, 16)) {
            p->alive = 0;
            if (g_carry < MAX_CARRY) {
                g_carry++;
                g_score += 20;
                toast("PIZZA PICKED UP +20");
            } else {
                g_score += 10;
                toast("BASKET FULL +10");
            }
        }
    }

    for (i = 0; i < MAX_OBST; i++) {
        struct obst *o = &g_obsts[i];
        s32 sx, sy, bw, bh;

        if (!o->alive)
            continue;
        sx = lane_x(o->lane) + o->xo;
        sy = (s32)(o->wy + g_scroll);
        if (sy > (s32)g_h + 50) {
            o->alive = 0;
            continue;
        }
        if (o->kind == OB_CONE) {
            bw = 14;
            bh = 16;
        } else if (o->kind == OB_CAR) {
            bw = 26;
            bh = 18;
        } else {
            bw = 16;
            bh = 12;
        }
        if (g_invuln == 0 &&
            box_overlap(g_pl_x - 12, g_pl_y - 16, 24, 32,
                        sx - bw / 2, sy - bh / 2, bw, bh)) {
            g_invuln = 1500;
            g_flash = 60;
            g_streak = 0;
            if (g_carry > 0) {
                g_carry--;
                toast("CRASH! PIZZA LOST");
            } else {
                toast("CRASH!");
            }
            g_lives--;
            if (g_lives <= 0) {
                if (g_score > g_hi)
                    g_hi = g_score;
                g_state = ST_OVER;
                return;
            }
        }
    }

    for (i = 0; i < MAX_HOUSE; i++) {
        struct house *h = &g_houses[i];
        s32 sy, dx;

        if (!h->alive)
            continue;
        sy = (s32)(h->wy + g_scroll);
        if (sy > (s32)g_h + 60) {
            h->alive = 0;
            continue;
        }
        dx = house_door_x(h);
        if (g_pl_x > dx - 22 && g_pl_x < dx + 22 &&
            sy > g_pl_y - 30 && sy < g_pl_y + 30) {
            if (!h->done && g_carry > 0 && h->num == g_target) {
                u32 gain;

                h->done = 1;
                g_carry--;
                g_delivered++;
                g_streak++;
                gain = 100u + (u32)(g_streak * 25);
                g_score += gain;
                do {
                    g_target = 1 + (int)(rng() % 9);
                } while (g_target == h->num && (rng() & 1));
                klibc.sprintf(g_toast, "DELIVERED #%d +%u", h->num, gain);
                g_toast_ms = 900;
                if (g_delivered % 3 == 0) {
                    g_base += 7.0f;
                    if (g_base > 150.0f)
                        g_base = 150.0f;
                }
            } else if (!h->done && g_carry > 0 && g_toast_ms == 0) {
                toast("WRONG HOUSE!");
            }
        }
    }
}

static void update(u32 dt_ms)
{
    if (g_state == ST_TITLE) {
        if (input_pressed(GKEY_SPACE) || input_pressed(GKEY_ENTER)) {
            game_init();
            g_state = ST_PLAY;
        }
    } else if (g_state == ST_PLAY) {
        update_play(dt_ms);
    } else {
        if (input_pressed(GKEY_ENTER) || input_pressed(GKEY_SPACE)) {
            game_init();
            g_state = ST_PLAY;
        }
    }
}

static void render(void)
{
    if (g_state == ST_TITLE) {
        render_title();
    } else {
        render_world();
        if (g_state == ST_OVER)
            render_over();
    }
}

void game_pizza_run(void)
{
    struct game g;

    if (!framebuffer_ready()) {
        klog("game.pizza", "no graphics framebuffer, skipping game");
        return;
    }

    engine_init("KERNEL PIZZA PATROL");

    g_w = gfx_width();
    g_h = gfx_height();
    g_rng = (u32)tsc_ms() ^ 0x9E3779B9u;
    g_state = ST_TITLE;
    g_hi = 0;

    g.init = game_init;
    g.update = update;
    g.render = render;
    g.run_ms = 0;
    g.quit = 0;

    engine_run(&g);

    input_clear();
}
