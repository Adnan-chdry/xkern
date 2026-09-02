/*
 * demo.c - "XKERN Breakout", a playable demo for the game engine.
 *
 * Controls:
 *   LEFT / RIGHT (or A / D)  move the paddle
 *   SPACE / ENTER             launch the ball / restart
 *   ESC                       quit the demo and continue booting
 *
 * The game is paused until the ball is launched.  When every brick is
 * cleared you win; dropping the ball three times is game over.
 */
#include "demo.h"
#include "game.h"
#include "IOGraphicsFamily/fb.h"
#include "klibc.h"
#include "klog.h"

#define BRICK_COLS 8
#define BRICK_ROWS 4

#define PADDLE_W 64
#define PADDLE_H 8
#define PADDLE_SPEED 420.0f

#define BALL_R 4

#define MAX_LIVES 3

static u32 g_w;
static u32 g_h;

static s32  g_paddle_x;
static s32  g_ball_x;
static s32  g_ball_y;
static float g_ball_vx;
static float g_ball_vy;
static float g_ball_speed;
static int  g_ball_stuck;
static s32  g_ball_off;   /* offset from paddle centre while stuck */

static s32  g_brick[BRICK_COLS][BRICK_ROWS];
static s32  g_brick_w;
static s32  g_brick_h;
static s32  g_brick_gap;
static s32  g_brick_top;

static u32  g_score;
static int  g_lives;
static int  g_won;
static int  g_over;

static u32 s_row_color[BRICK_ROWS] = {
    GFX_RED, GFX_ORANGE, GFX_YELLOW, GFX_GREEN
};

/* 8x8 "heart" sprite for the lives indicator. */
static const u8 s_heart[] = {
    0x6C, 0xFE, 0xFE, 0xFE, 0x7C, 0x38, 0x10, 0x00,
};

static void demo_init(void)
{
    u32 r, c;

    g_w = gfx_width();
    g_h = gfx_height();

    g_brick_gap = 4;
    g_brick_h = 14;
    g_brick_w = ((s32)g_w - (s32)(BRICK_COLS + 1) * g_brick_gap) /
                (s32)BRICK_COLS;
    g_brick_top = 40;

    for (r = 0; r < BRICK_ROWS; r++)
        for (c = 0; c < BRICK_COLS; c++)
            g_brick[c][r] = 1;

    g_score = 0;
    g_lives = MAX_LIVES;
    g_won = 0;
    g_over = 0;
    g_ball_speed = 260.0f;

    g_paddle_x = (s32)(g_w - PADDLE_W) / 2;
    g_ball_stuck = 1;
    g_ball_off = 0;
}

static void ball_reset(void)
{
    g_ball_stuck = 1;
    g_ball_off = 0;
    g_ball_speed = 260.0f;
}

static void launch_ball(void)
{
    g_ball_stuck = 0;
    g_ball_vx = 0.0f;
    g_ball_vy = -g_ball_speed;
}

static int aabb(s32 ax, s32 ay, u32 aw, u32 ah,
                s32 bx, s32 by, u32 bw, u32 bh)
{
    return ax < bx + (s32)bw && ax + (s32)aw > bx &&
           ay < by + (s32)bh && ay + (s32)ah > by;
}

static void bricks_left(s32 *n)
{
    u32 r, c;

    *n = 0;
    for (r = 0; r < BRICK_ROWS; r++)
        for (c = 0; c < BRICK_COLS; c++)
            if (g_brick[c][r])
                (*n)++;
}

static void ball_collide_bricks(void)
{
    u32 r, c;

    for (r = 0; r < BRICK_ROWS; r++) {
        for (c = 0; c < BRICK_COLS; c++) {
            s32 bx, by;
            s32 dx, dy;

            if (!g_brick[c][r])
                continue;

            bx = (s32)c * (g_brick_w + g_brick_gap) + g_brick_gap;
            by = g_brick_top + (s32)r * (g_brick_h + g_brick_gap);

            if (!aabb(g_ball_x - BALL_R, g_ball_y - BALL_R,
                      2 * BALL_R, 2 * BALL_R, bx, by,
                      (u32)g_brick_w, (u32)g_brick_h))
                continue;

            g_brick[c][r] = 0;
            g_score += 10 * (u32)(BRICK_ROWS - r);
            g_ball_speed += 4.0f;

            dx = g_ball_x - (bx + g_brick_w / 2);
            dy = g_ball_y - (by + g_brick_h / 2);
            if (dx < 0) dx = -dx;
            if (dy < 0) dy = -dy;

            if (dx > dy)
                g_ball_vx = -g_ball_vx;
            else
                g_ball_vy = -g_ball_vy;

            return;
        }
    }
}

static void demo_update(u32 dt_ms)
{
    s32 n;
    float dt = (float)dt_ms / 1000.0f;

    if (g_over || g_won) {
        if (input_pressed(GKEY_ENTER) || input_pressed(GKEY_SPACE)) {
            demo_init();
        }
        return;
    }

    if (input_key(GKEY_LEFT) || input_key(GKEY_A))
        g_paddle_x -= (s32)(PADDLE_SPEED * dt);
    if (input_key(GKEY_RIGHT) || input_key(GKEY_D))
        g_paddle_x += (s32)(PADDLE_SPEED * dt);

    if (g_paddle_x < 0)
        g_paddle_x = 0;
    if (g_paddle_x + (s32)PADDLE_W > (s32)g_w)
        g_paddle_x = (s32)g_w - (s32)PADDLE_W;

    if (g_ball_stuck) {
        g_ball_x = g_paddle_x + (s32)PADDLE_W / 2 + g_ball_off;
        g_ball_y = g_h - 32 - BALL_R;

        if (input_pressed(GKEY_SPACE) || input_pressed(GKEY_ENTER))
            launch_ball();
        return;
    }

    g_ball_x += (s32)(g_ball_vx * dt);
    g_ball_y += (s32)(g_ball_vy * dt);

    if (g_ball_x - BALL_R < 0) {
        g_ball_x = BALL_R;
        g_ball_vx = -g_ball_vx;
    }
    if (g_ball_x + BALL_R > (s32)g_w) {
        g_ball_x = (s32)g_w - BALL_R;
        g_ball_vx = -g_ball_vx;
    }
    if (g_ball_y - BALL_R < 0) {
        g_ball_y = BALL_R;
        g_ball_vy = -g_ball_vy;
    }

    if (g_ball_y - BALL_R > (s32)g_h) {
        g_lives--;
        if (g_lives <= 0) {
            g_over = 1;
        } else {
            ball_reset();
        }
        return;
    }

    if (g_ball_vy > 0.0f &&
        aabb(g_ball_x - BALL_R, g_ball_y - BALL_R, 2 * BALL_R, 2 * BALL_R,
             g_paddle_x, g_h - 32, PADDLE_W, PADDLE_H)) {
        float rel = (float)(g_ball_x - (g_paddle_x + (s32)PADDLE_W / 2)) /
                    (float)(PADDLE_W / 2);

        if (rel < -1.0f) rel = -1.0f;
        if (rel > 1.0f) rel = 1.0f;

        g_ball_vx = rel * g_ball_speed * 0.7f;
        g_ball_vy = -g_ball_speed * 0.9f;
        g_ball_y = (s32)(g_h - 32 - BALL_R);
    }

    ball_collide_bricks();

    bricks_left(&n);
    if (n == 0)
        g_won = 1;
}

static void draw_hud(void)
{
    char buf[64];
    int i;

    klibc.sprintf(buf, "SCORE %u", g_score);
    gfx_text(8, 8, buf, GFX_WHITE, GFX_BLACK);

    klibc.sprintf(buf, "%u FPS", GAME_FPS);
    gfx_text((s32)((g_w - gfx_text_width(buf)) / 2), 8, buf, GFX_CYAN, GFX_BLACK);

    for (i = 0; i < MAX_LIVES; i++) {
        u32 col = (i < g_lives) ? GFX_RED : GFX_RGB(0x30, 0x30, 0x30);

        gfx_sprite((s32)(g_w - 16 - i * 10), 8, 8, 8, s_heart,
                   col, GFX_BLACK, 0);
    }
}

static void demo_render(void)
{
    u32 r, c;
    char buf[64];

    gfx_clear(GFX_RGB(0x08, 0x10, 0x20));

    for (r = 0; r < BRICK_ROWS; r++) {
        for (c = 0; c < BRICK_COLS; c++) {
            s32 bx, by;

            if (!g_brick[c][r])
                continue;

            bx = (s32)c * (g_brick_w + g_brick_gap) + g_brick_gap;
            by = g_brick_top + (s32)r * (g_brick_h + g_brick_gap);

            gfx_rect(bx, by, (u32)g_brick_w, (u32)g_brick_h,
                     s_row_color[r]);
        }
    }

    gfx_rect(g_paddle_x, (s32)g_h - 32, PADDLE_W, PADDLE_H, GFX_WHITE);

    if (g_ball_stuck)
        gfx_circle_outline(g_ball_x, g_ball_y, BALL_R, GFX_YELLOW);
    else
        gfx_circle(g_ball_x, g_ball_y, BALL_R, GFX_YELLOW);

    draw_hud();

    if (g_over) {
        gfx_rect(0, (s32)g_h / 2 - 20, g_w, 40, GFX_BLACK);
        gfx_rect_outline(0, (s32)g_h / 2 - 20, g_w, 40, GFX_RED);
        gfx_text((s32)((g_w - gfx_text_width("GAME OVER")) / 2),
                 (s32)g_h / 2 - 14, "GAME OVER", GFX_RED, GFX_BLACK);
        gfx_text((s32)((g_w - gfx_text_width("PRESS ENTER TO RESTART")) / 2),
                 (s32)g_h / 2 - 2,
                 "PRESS ENTER TO RESTART", GFX_WHITE, GFX_BLACK);
    } else if (g_won) {
        gfx_rect(0, (s32)g_h / 2 - 20, g_w, 40, GFX_BLACK);
        gfx_rect_outline(0, (s32)g_h / 2 - 20, g_w, 40, GFX_GREEN);
        gfx_text((s32)((g_w - gfx_text_width("YOU WIN")) / 2),
                 (s32)g_h / 2 - 14, "YOU WIN", GFX_GREEN, GFX_BLACK);
        klibc.sprintf(buf, "FINAL SCORE %u", g_score);
        gfx_text((s32)((g_w - gfx_text_width(buf)) / 2),
                 (s32)g_h / 2 - 2, buf, GFX_WHITE, GFX_BLACK);
    } else if (g_ball_stuck) {
        gfx_text((s32)((g_w - gfx_text_width("PRESS SPACE TO LAUNCH")) / 2),
                 (s32)g_h - 48, "PRESS SPACE TO LAUNCH", GFX_WHITE, GFX_BLACK);
    }

    /* splash footer */
    gfx_text(8, (s32)g_h - 12, "XKERN 2D ENGINE", GFX_MAGENTA, GFX_BLACK);
}

void game_demo_run(void)
{
    struct game g;

    if (!framebuffer_ready()) {
        klog("game.demo", "no graphics framebuffer, skipping demo");
        return;
    }

    engine_init("XKERN BREAKOUT");

    g.init = demo_init;
    g.update = demo_update;
    g.render = demo_render;
    g.run_ms = 0;
    g.quit = 0;

    engine_run(&g);

    input_clear();
}
