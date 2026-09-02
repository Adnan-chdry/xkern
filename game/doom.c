/*
 * doom.c - "XKERN DOOM", a Doom-style first-person raycaster.
 *
 * Wolfenstein-class engine built on the xkern game framework:
 *   - DDA raycasting with textured walls (procedural 64x64 textures)
 *   - texture-cast checker floor, gradient sky
 *   - billboard sprites (imps, health, ammo) depth-sorted + z-buffered
 *   - hitscan weapon with recoil / muzzle flash, imps that chase and
 *     throw fireballs, doors, pickups and an exit tile
 *   - HUD (health/ammo/kills), crosshair, toggleable minimap
 *
 * The scene is raycast at half resolution into a pmm-backed render
 * buffer, then nearest-neighbour 2x upscaled to the framebuffer for
 * speed (this file is compiled with -O2 in the Makefile).
 *
 * Controls:
 *   W/S          forward / back
 *   A/D          strafe
 *   LEFT/RIGHT   turn
 *   MOUSE x      turn (moves independently of arrows)
 *   MOUSE left / ENTER   fire
 *   SPACE / E    open nearby door
 *   M            toggle minimap
 *   ESC          quit back to boot
 */
#include "doom.h"
#include "game.h"
#include "IOGraphicsFamily/fb.h"
#include "klibc.h"
#include "klog.h"
#include "pmm.h"
#include "paging.h"

/* color helpers (gfx.h provides GFX_*) */
#define GUK_RGB(r, g, b)   GFX_RGB(r, g, b)
#define GUK_BLACK          GFX_BLACK
#define GUK_WHITE          GFX_WHITE
#define GUK_RED            GFX_RED
#define GUK_GREEN          GFX_GREEN
#define GUK_YELLOW         GFX_YELLOW
#define GUK_CYAN           GFX_CYAN
#define GUK_GRAY           GFX_RGB(0x80, 0x80, 0x88)

/* --- tunables ------------------------------------------------------------- */

#define MAP_W           24
#define MAP_H           16
#define MOVE_SPEED      4.0f     /* units / second */
#define TURN_SPEED      2.8f     /* rad / second (arrow keys) */
#define MOUSE_SENS      0.0045f
#define PLANE_MAG       0.66f    /* camera plane magnitude (~66 deg FOV) */
#define MAX_HP          100
#define START_AMMO      50
#define HUD_H           44
#define FIRE_CD_MS      260
#define DAMAGE_FLASH_MS 160

#define T_BRICK 0
#define T_TECH  1
#define T_WOOD  2
#define T_STONE 3
#define T_DOOR  4
#define T_EXIT  5

#define E_IMP    0
#define E_HEALTH 1
#define E_AMMO   2

#define ST_INTRO 0
#define ST_PLAY  1
#define ST_DEAD  2
#define ST_WIN   3

/* --- sprites (palette indices, 0 = transparent) --------------------------- */

#define SPR_IMP_W  16
#define SPR_IMP_H  16
static const char *const s_imp_map[SPR_IMP_H] = {
    "................",
    "......4444......",
    ".....4aaaa4.....",
    "....4aaaaaaaa4..",
    "....4aaaaaaaa4..",
    "...4aa33aa33aa4.",
    "...4aaaaaaaaaa4.",
    "...4aa4aa4aa4a4.",
    "...4aaaaaaaaaa4.",
    "...4a44a44a44a4.",
    "....4aaaaaaaa4..",
    "....44a4444a44..",
    ".....4aaaaaa4...",
    "....4a4aaaa4a4..",
    "....44a44a44a4..",
    "................",
};

#define SPR_ITEM_W 8
#define SPR_ITEM_H 8
static const char *const s_health_map[SPR_ITEM_H] = {
    "........",
    ".wwwwww.",
    ".wggggw.",
    ".wggggw.",
    ".wggggw.",
    ".wggggw.",
    ".wwwwww.",
    "........",
};

static const char *const s_ammo_map[SPR_ITEM_H] = {
    "........",
    "..yyyy..",
    "..ygggg.",
    "..yyyy..",
    "..gggg..",
    "..yyyy..",
    "..gggg..",
    "........",
};

static const char *const s_ball_map[SPR_ITEM_H] = {
    "........",
    "...oo...",
    "..oooo..",
    ".oooooo.",
    ".oooooo.",
    "..oooo..",
    "...oo...",
    "........",
};

/* imp palette */
static const u32 s_imp_pal[5] = {
    0x000000,                       /* 0 transparent (unused) */
    0x8A1F1F,                       /* 1 dark flesh */
    0xC84343,                       /* 2 red */
    0xFFD24A,                       /* 3 eyes */
    0x1A0000,                       /* 4 outline / horns */
};
static const u32 s_health_pal[3] = { 0x000000, 0xFFFFFF, 0x2E8B57 };
static const u32 s_ammo_pal[3]   = { 0x000000, 0xFFD24A, 0x66666C };
static const u32 s_ball_pal[2]   = { 0x000000, 0xFF9F0A };

/* --- map ------------------------------------------------------------------ */

/* Legend: '#/1..4' walls, 'D' door, 'I' imp, 'H' health, 'A' ammo, 'P'
 * player start, 'E' exit tile.  Rows are padded to MAP_W at init. */
static const char *const g_map[MAP_H] = {
    "########################",
    "#1..........A........2.#",
    "#.I....HH....D.....I...#",
    "#..1..1................#",
    "#..1..1.......1........#",
    "#.......1..I..1........#",
    "#A..D...1......1...D...#",
    "#..1....1..P...1....1..#",
    "#..1....1......1....1..#",
    "#..1....1......1....1..#",
    "#.......1......1...1...#",
    "#.I.....1..I..1...1..A.#",
    "#..............1.......#",
    "#..1..1..1..1..1..1....#",
    "#.......E...............#",
    "########################",
};

/* --- entities ------------------------------------------------------------- */

#define MAX_ENTS 40
#define MAX_PROJ 24

struct ent {
    int type;
    int alive;
    float x, y;
    int hp;
    float t;     /* behaviour timer */
};

struct proj {
    int alive;
    float x, y, vx, vy;
};

/* --- engine state --------------------------------------------------------- */

static u32 g_screen_w, g_screen_h;   /* framebuffer (full res) */
static u32 g_iw, g_ih;               /* internal render resolution (half) */

static u32 *g_buf;                   /* internal frame buffer (pmm) */
static u32 g_buf_pages;
static float *g_zbuf;                /* per-column z buffer (pmm) */
static u32 g_zbuf_pages;

static u32 g_tex[6][64 * 64];        /* procedural wall textures */
static u8  g_wallmap[MAP_W][MAP_H];  /* cell -> texture (0 = empty) */
static float g_door[MAP_W][MAP_H];   /* door openness 0..1 */

static u8  g_imp_spr[SPR_IMP_H][SPR_IMP_W];
static u8  g_health_spr[SPR_ITEM_H][SPR_ITEM_W];
static u8  g_ammo_spr[SPR_ITEM_H][SPR_ITEM_W];
static u8  g_ball_spr[SPR_ITEM_H][SPR_ITEM_W];

static struct ent g_ents[MAX_ENTS];
static struct proj g_proj[MAX_PROJ];
static int g_ent_count, g_imp_total, g_imp_kills;

static float g_px, g_py, g_pa;       /* player pos + angle (rad) */
static int   g_hp, g_ammo;
static int   g_state;
static int   g_minimap;
static u32   g_bob;                  /* walk-bob phase */
static int   g_recoil;               /* weapon kick-back px */
static u32   g_flash;                /* muzzle flash timer */
static u32   g_firecd;               /* fire cooldown */
static u32   g_hurt;                 /* damage flash timer */
static char  g_msg[56];
static u32   g_msg_t;
static int   g_exitx, g_exity;

/* sin/cos lookup (2048 entries) */
#define STAB 2048
static float g_sin[STAB], g_cos[STAB];

static u32 g_shade_dark[256], g_shade_light[256];   /* pre-shaded floor */
static u32 g_sky[1600];                             /* per-row sky gradient */

/* --- tiny math ------------------------------------------------------------- */

static inline float f_abs(float v) { return v < 0.0f ? -v : v; }

#define TWO_PI 6.28318530718
#define PI     3.14159265359

static inline double my_sin(double x)
{
    double x2;
    double s;
    int neg = 0;

    while (x < -PI) x += TWO_PI;
    while (x > PI)  x -= TWO_PI;
    if (x < 0) { x = -x; neg = 1; }
    if (x > PI / 2) x = PI - x;
    x2 = x * x;
    s = x * (1.0 - x2 / 6.0 *
            (1.0 - x2 / 20.0 * (1.0 - x2 / 42.0 * (1.0 - x2 / 72.0))));
    return neg ? -s : s;
}

static void build_trig(void)
{
    u32 i;
    for (i = 0; i < STAB; i++) {
        double a = (double)i * (TWO_PI / (double)STAB);
        g_sin[i] = (float)my_sin(a);
        g_cos[i] = (float)my_sin(a + PI / 2);
    }
}

static inline float tsin(float a) { return g_sin[((int)(a * (STAB / TWO_PI))) & (STAB - 1)]; }
static inline float tcos(float a) { return g_cos[((int)(a * (STAB / TWO_PI))) & (STAB - 1)]; }

static u32 mul_chan(u32 c, int m, int k)
{
    return (u32)((((c >> m) & 0xFF) * k + 128) >> 8);
}

/* scale a 0xRRGGBB color so full brightness is 255 */
static u32 shade_color(u32 c, int k)
{
    if (k >= 255)
        return c;
    if (k < 1)
        k = 1;
    return (mul_chan(c, 16, k) << 16) | (mul_chan(c, 8, k) << 8) | mul_chan(c, 0, k);
}

/* --- map helpers ----------------------------------------------------------- */

static int cell_is_wall(int x, int y)
{
    if (x < 0 || y < 0 || x >= MAP_W || y >= MAP_H)
        return 1;
    if (!g_wallmap[x][y])
        return 0;
    if (g_wallmap[x][y] == T_DOOR && g_door[x][y] >= 0.6f)
        return 0;                /* open doors are passable */
    return 1;
}

static int cell_is_solid(int x, int y)
{
    return cell_is_wall(x, y);
}

/* --- textures -------------------------------------------------------------- */

static u32 thash(u32 x, u32 y, u32 seed)
{
    u32 h = x * 374761393u + y * 668265263u + seed * 1442695041u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return h ^ (h >> 16);
}

static void tex_brick(u32 *t)
{
    u32 x, y;
    for (y = 0; y < 64; y++) {
        for (x = 0; x < 64; x++) {
            int row = (int)(y / 16);
            int xo = (int)x;
            u32 base;
            u32 n = thash(x, y, 7) % 9;
            if (row & 1)
                xo -= 8;
            if (y % 16 == 15 || xo % 16 == 0)
                base = GUK_RGB(0x20, 0x1A, 0x1A);
            else
                base = GUK_RGB(0x74 - n, 0x28 - (n >> 1), 0x26 - (n >> 1));
            t[y * 64 + x] = base;
        }
    }
}

static void tex_tech(u32 *t)
{
    u32 x, y;
    for (y = 0; y < 64; y++) {
        for (x = 0; x < 64; x++) {
            u32 n = thash(x, y, 13) % 5;
            u32 c = GUK_RGB(0x58 - n, 0x5E - n, 0x6A - n);
            if (y % 16 == 0 || y % 16 == 15)
                c = GUK_RGB(0x2E, 0x32, 0x3C);          /* panel edge */
            if ((x == 31 || x == 32) && y < 40)
                c = GUK_RGB(0x8A, 0x9A, 0xB8);          /* seam */
            if (x % 16 == 0 && y % 16 == 4)
                c = GUK_RGB(0x9A, 0xA6, 0xBE);          /* rivet */
            t[y * 64 + x] = c;
        }
    }
}

static void tex_wood(u32 *t)
{
    u32 x, y;
    for (y = 0; y < 64; y++) {
        for (x = 0; x < 64; x++) {
            u32 n = thash(x, y, 29) % 11;
            u32 c = GUK_RGB(0x63 - (n >> 1), 0x45 - (n >> 1), 0x24 - (n >> 2));
            if (x % 16 == 0)
                c = GUK_RGB(0x2B, 0x1B, 0x0E);          /* plank seam */
            t[y * 64 + x] = c;
        }
    }
}

static void tex_stone(u32 *t)
{
    u32 x, y;
    for (y = 0; y < 64; y++) {
        for (x = 0; x < 64; x++) {
            u32 n = thash(x, y, 3) % 7;
            u32 c = GUK_RGB(0x5A + n, 0x5A + n, 0x60 + n);
            if (y % 32 == 0 || x % 32 == 0)
                c = GUK_RGB(0x2E, 0x2E, 0x33);          /* block seam */
            t[y * 64 + x] = c;
        }
    }
}

static void tex_door(u32 *t)
{
    u32 x, y;
    for (y = 0; y < 64; y++) {
        for (x = 0; x < 64; x++) {
            u32 n = thash(x, y, 41) % 5;
            u32 c = GUK_RGB(0x3E + (n >> 1), 0x41 + (n >> 1), 0x48 + n);
            if (y < 3 || y > 60)
                c = GUK_RGB(0x18, 0x18, 0x1E);          /* frame */
            if (y >= 22 && y <= 42 && x >= 8 && x <= 24)
                c = GUK_RGB(0x63, 0x74, 0x8C);          /* window */
            if (x == 4 || x == 5)
                c = GUK_RGB(0x9A, 0xA6, 0xBE);          /* handle plate */
            t[y * 64 + x] = c;
        }
    }
}

static void tex_exit(u32 *t)
{
    u32 x, y;
    for (y = 0; y < 64; y++) {
        for (x = 0; x < 64; x++) {
            u32 n = thash(x, y, 61) % 4;
            u32 c = GUK_RGB(0x1C + n, 0x2C + n, 0x1C + n);
            if (x % 8 == 0 || x % 8 == 7)
                c = GUK_RGB(0x22, 0x22, 0x22);          /* grate */
            if ((x / 8) % 2 == 0 && y > 8 && y < 56)
                c = GUK_RGB(0x28, 0xC8, 0x40);          /* glow bars */
            t[y * 64 + x] = c;
        }
    }
}

static void build_textures(void)
{
    tex_brick(g_tex[T_BRICK]);
    tex_tech(g_tex[T_TECH]);
    tex_wood(g_tex[T_WOOD]);
    tex_stone(g_tex[T_STONE]);
    tex_door(g_tex[T_DOOR]);
    tex_exit(g_tex[T_EXIT]);
}

/* --- sprite setup ---------------------------------------------------------- */

static u8 spr_idx(char ch)
{
    switch (ch) {
    case 'a': case 'w': case '1': return 1;
    case '2': case 'g': case 'o': return 2;
    case '3': case 'y': return 3;
    case '4': return 4;
    default: return 0;
    }
}

static void spr_convert8(u8 out[][SPR_ITEM_W], const char *const *rows, u32 h)
{
    u32 y, x;
    for (y = 0; y < h; y++)
        for (x = 0; x < SPR_ITEM_W; x++)
            out[y][x] = spr_idx(rows[y][x]);
}

static void build_sprites(void)
{
    u32 y, x;

    for (y = 0; y < SPR_IMP_H; y++)
        for (x = 0; x < SPR_IMP_W; x++)
            g_imp_spr[y][x] = spr_idx(s_imp_map[y][x]);
    spr_convert8(g_health_spr, s_health_map, SPR_ITEM_H);
    spr_convert8(g_ammo_spr, s_ammo_map, SPR_ITEM_H);
    spr_convert8(g_ball_spr, s_ball_map, SPR_ITEM_H);
}

/* --- level setup ------------------------------------------------------------ */

static void parse_map(void)
{
    int x, y;
    klibc.memset(g_wallmap, 0, sizeof(g_wallmap));
    klibc.memset(g_door, 0, sizeof(g_door));

    for (y = 0; y < MAP_H; y++) {
        for (x = 0; x < MAP_W; x++) {
            char ch = g_map[y][x] ? g_map[y][x] : '.';
            switch (ch) {
            case '#': case '1': g_wallmap[x][y] = T_BRICK; break;
            case '2': g_wallmap[x][y] = T_TECH;  break;
            case '3': g_wallmap[x][y] = T_WOOD;  break;
            case '4': g_wallmap[x][y] = T_STONE; break;
            case 'D': g_wallmap[x][y] = T_DOOR;  break;
            case 'P': g_px = (float)x + 0.5f; g_py = (float)y + 0.5f; break;
            case 'E': g_exitx = x; g_exity = y; break;
            default: break;
            }
        }
    }
}

static void spawn_entities(void)
{
    int x, y, n = 0;

    g_ent_count = 0;
    g_imp_total = 0;
    g_imp_kills = 0;

    for (y = 0; y < MAP_H; y++)
        for (x = 0; x < MAP_W; x++) {
            int type = -1;
            char ch = g_map[y][x] ? g_map[y][x] : '.';
            if (ch == 'I') type = E_IMP;
            else if (ch == 'H') type = E_HEALTH;
            else if (ch == 'A') type = E_AMMO;
            if (type < 0 || n >= MAX_ENTS)
                continue;
            g_ents[n].type = type;
            g_ents[n].alive = 1;
            g_ents[n].x = (float)x + 0.5f;
            g_ents[n].y = (float)y + 0.5f;
            g_ents[n].hp = 30;
            g_ents[n].t = 0.0f;
            if (type == E_IMP)
                g_imp_total++;
            n++;
        }
    g_ent_count = n;
}

static void init_level(void)
{
    parse_map();
    spawn_entities();

    g_pa = 0.0f;
    g_hp = MAX_HP;
    g_ammo = START_AMMO;
    g_firecd = 0;
    g_flash = 0;
    g_recoil = 0;
    g_hurt = 0;
    g_bob = 0;
    g_msg[0] = '\0';
    g_msg_t = 0;
    g_minimap = 0;

    {
        int i;
        for (i = 0; i < MAX_PROJ; i++)
            g_proj[i].alive = 0;
    }
}

/* --- shading tables -------------------------------------------------------- */

static u32 sky_row(u32 row)
{
    /* dark blue top -> hazy grey at the horizon */
    int k = (int)(255 - row * 140 / (g_ih ? g_ih : 1));
    if (k < 40) k = 40;
    return shade_color(GUK_RGB(0x2A, 0x34, 0x4E), k);
}

static void build_shade_tables(void)
{
    u32 i;
    u32 hmax = g_ih ? g_ih : 1;
    for (i = 0; i < 256; i++)
        g_shade_dark[i]  = shade_color(GUK_RGB(0x2E, 0x2E, 0x34), (int)i);
    for (i = 0; i < 256; i++)
        g_shade_light[i] = shade_color(GUK_RGB(0x4A, 0x4A, 0x54), (int)i);
    for (i = 0; i < hmax; i++)
        g_sky[i] = sky_row(i);
}

/* --- level logic (PLAY) ---------------------------------------------------- */

static int los_clear(float x0, float y0, float x1, float y1)
{
    float dx = x1 - x0, dy = y1 - y0;
    float d = f_abs(dx) > f_abs(dy) ? f_abs(dx) : f_abs(dy);
    float step = 0.25f;
    float t;

    if (d < 0.01f)
        return 1;
    for (t = 0.0f; t < d; t += step) {
        if (cell_is_solid((int)(x0 + dx * t / d), (int)(y0 + dy * t / d)))
            return 0;
    }
    return 1;
}

static void fire_weapon(void)
{
    float dx = tcos(g_pa), dy = tsin(g_pa);
    float px = g_px, py = g_py;
    float d;

    g_flash = FIRE_CD_MS;
    g_recoil = 7;
    g_firecd = FIRE_CD_MS;

    if (g_ammo <= 0) {
        klibc.snprintf(g_msg, sizeof(g_msg), "OUT OF AMMO");
        g_msg_t = 1200;
        return;
    }
    g_ammo--;

    for (d = 0.0f; d < 12.0f; d += 0.1f) {
        int cx = (int)(px + dx * d), cy = (int)(py + dy * d);
        int i;

        if (cell_is_solid(cx, cy))
            return;

        for (i = 0; i < g_ent_count; i++) {
            struct ent *e = &g_ents[i];
            if (!e->alive || e->type != E_IMP)
                continue;
            if (f_abs(e->x - (px + dx * d)) < 0.4f &&
                f_abs(e->y - (py + dy * d)) < 0.4f) {
                e->hp -= 16;
                if (e->hp <= 0) {
                    e->alive = 0;
                    g_imp_kills++;
                    klibc.snprintf(g_msg, sizeof(g_msg),
                                   "IMP DOWNED (+%d)", 100);
                } else {
                    klibc.snprintf(g_msg, sizeof(g_msg), "IMP HIT");
                }
                g_msg_t = 1800;
                return;
            }
        }
    }
}

static void try_open_door(void)
{
    int cx = (int)g_px, cy = (int)g_py;
    int dx[4] = {1, -1, 0, 0}, dy[4] = {0, 0, 1, -1};
    int i;

    for (i = 0; i < 4; i++) {
        int x = cx + dx[i], y = cy + dy[i];
        if (x >= 0 && y >= 0 && x < MAP_W && y < MAP_H &&
            g_wallmap[x][y] == T_DOOR && g_door[x][y] < 0.1f) {
            g_door[x][y] = 0.1f;   /* start opening */
            klibc.snprintf(g_msg, sizeof(g_msg), "DOOR OPENING");
            g_msg_t = 1500;
            return;
        }
    }
}

static void update_player(u32 dt)
{
    float fdt = (float)dt / 1000.0f;
    struct input_mouse m;
    float fwd = 0.0f, strafe = 0.0f;
    float move, nx, ny;
    int mstep = 0;

    if (input_key(GKEY_UP) || input_key(GKEY_W))
        fwd += 1.0f;
    if (input_key(GKEY_DOWN) || input_key(GKEY_S))
        fwd -= 1.0f;
    if (input_key(GKEY_D))
        strafe += 1.0f;
    if (input_key(GKEY_A))
        strafe -= 1.0f;

    if (input_key(GKEY_LEFT))
        g_pa += TURN_SPEED * fdt;
    if (input_key(GKEY_RIGHT))
        g_pa -= TURN_SPEED * fdt;

    if (input_mouse(&m)) {
        g_pa -= (float)m.dx * MOUSE_SENS;
        if (m.buttons & 1 && g_firecd == 0)
            fire_weapon();
    }
    if (input_pressed(GKEY_ENTER) && g_firecd == 0)
        fire_weapon();
    if (input_pressed(GKEY_SPACE) || input_pressed(GKEY_E))
        try_open_door();
    if (input_pressed(GKEY_M))
        g_minimap = !g_minimap;

    if (g_firecd > dt)
        g_firecd -= dt;
    else
        g_firecd = 0;
    if (g_flash > dt)
        g_flash -= dt;
    else
        g_flash = 0;
    if (g_recoil > 0) {
        g_recoil--;
        if (g_recoil < 0)
            g_recoil = 0;
    }
    if (g_hurt > dt)
        g_hurt -= dt;
    else
        g_hurt = 0;
    if (g_msg_t > dt)
        g_msg_t -= dt;
    else
        g_msg_t = 0;

    /* move with collision (per axis) */
    move = MOVE_SPEED * fdt;
    if (fwd != 0.0f || strafe != 0.0f) {
        float dx = tcos(g_pa), dy = tsin(g_pa);
        float rx = dy, ry = -dx;      /* right = (sin, -cos) */
        nx = g_px + (dx * fwd + rx * strafe) * move;
        ny = g_py + (dy * fwd + ry * strafe) * move;

        if (!cell_is_solid((int)(nx + (dx * fwd > 0 ? 0.2f : -0.2f)),
                           (int)(g_py + (dy * fwd > 0 ? 0.2f : -0.2f))))
            g_px = nx;
        if (!cell_is_solid((int)(g_px + (dx * fwd > 0 ? 0.2f : -0.2f)),
                           (int)(ny + (dy * fwd > 0 ? 0.2f : -0.2f))))
            g_py = ny;

        if (cell_is_solid((int)(g_px + dx * 0.3f), (int)(g_py + dy * 0.3f))) {
            /* bumping into a closed door auto-opens it */
            int dxc = (int)(g_px + dx * 0.4f), dyc = (int)(g_py + dy * 0.4f);
            if (g_wallmap[dxc][dyc] == T_DOOR && g_door[dxc][dyc] < 0.1f)
                g_door[dxc][dyc] = 0.1f;
        }
        g_bob += 6;
        mstep = 1;
    }
    (void)mstep;

    /* bound to the map */
    if (g_px < 0.3f) g_px = 0.3f;
    if (g_py < 0.3f) g_py = 0.3f;
    if (g_px > (float)(MAP_W - 1) - 0.3f) g_px = (float)(MAP_W - 1) - 0.3f;
    if (g_py > (float)(MAP_H - 1) - 0.3f) g_py = (float)(MAP_H - 1) - 0.3f;
}

static void update_enemies(u32 dt)
{
    float fdt = (float)dt / 1000.0f;
    int i;

    for (i = 0; i < g_ent_count; i++) {
        struct ent *e = &g_ents[i];
        float dx, dy, d;
        int pj;

        if (!e->alive || e->type != E_IMP)
            continue;

        dx = g_px - e->x;
        dy = g_py - e->y;
        d = f_abs(dx) > f_abs(dy) ? f_abs(dx) : f_abs(dy);
        if (d < 0.01f)
            d = 0.01f;

        e->t -= fdt;

        if (d < 1.3f && e->t <= 0.0f) {
            /* melee bite */
            g_hp -= 5;
            g_hurt = DAMAGE_FLASH_MS;
            e->t = 1.0f;
        } else if (d > 2.0f && d < 9.0f && e->t <= 0.0f &&
                   los_clear(e->x, e->y, g_px, g_py)) {
            /* lob a fireball */
            for (pj = 0; pj < MAX_PROJ; pj++)
                if (!g_proj[pj].alive)
                    break;
            if (pj < MAX_PROJ) {
                float s = 5.0f / d;
                g_proj[pj].alive = 1;
                g_proj[pj].x = e->x;
                g_proj[pj].y = e->y;
                g_proj[pj].vx = dx * s;
                g_proj[pj].vy = dy * s;
            }
            e->t = 2.5f;
        } else if (d > 1.4f && los_clear(e->x, e->y, g_px, g_py)) {
            /* advance on the player */
            float nx = e->x + dx / d * 1.2f * fdt;
            float ny = e->y + dy / d * 1.2f * fdt;
            if (!cell_is_solid((int)(nx + dx / d * 0.3f), (int)e->y))
                e->x = nx;
            if (!cell_is_solid((int)e->x, (int)(ny + dy / d * 0.3f)))
                e->y = ny;
        }
    }
}

static void update_projectiles(u32 dt)
{
    float fdt = (float)dt / 1000.0f;
    int i;

    for (i = 0; i < MAX_PROJ; i++) {
        struct proj *p = &g_proj[i];
        float dx, dy, d2;

        if (!p->alive)
            continue;
        p->x += p->vx * fdt;
        p->y += p->vy * fdt;

        dx = p->x - g_px;
        dy = p->y - g_py;
        d2 = dx * dx + dy * dy;

        if (d2 < 0.16f) {
            g_hp -= 6;
            g_hurt = DAMAGE_FLASH_MS;
            p->alive = 0;
            klibc.snprintf(g_msg, sizeof(g_msg), "HIT BY FIREBALL");
            g_msg_t = 1500;
        } else if (cell_is_solid((int)p->x, (int)p->y)) {
            p->alive = 0;
        }
    }
}

static void update_pickups_and_exit(void)
{
    int i;

    for (i = 0; i < g_ent_count; i++) {
        struct ent *e = &g_ents[i];
        float dx, dy;

        if (!e->alive || e->type == E_IMP)
            continue;
        dx = e->x - g_px;
        dy = e->y - g_py;
        if (dx * dx + dy * dy < 0.25f) {
            e->alive = 0;
            if (e->type == E_HEALTH) {
                g_hp += 25;
                if (g_hp > MAX_HP) g_hp = MAX_HP;
                klibc.snprintf(g_msg, sizeof(g_msg), "PICKED UP A HEALTH PACK");
            } else {
                g_ammo += 20;
                klibc.snprintf(g_msg, sizeof(g_msg), "PICKED UP AMMO (+20)");
            }
            g_msg_t = 1800;
        }
    }

    if ((int)g_px == g_exitx && (int)g_py == g_exity) {
        g_state = ST_WIN;
        g_msg_t = 0;
    }
}

static void update_doors(u32 dt)
{
    float fdt = (float)dt / 1000.0f;
    int x, y;

    for (y = 0; y < MAP_H; y++)
        for (x = 0; x < MAP_W; x++)
            if (g_door[x][y] > 0.0f && g_door[x][y] < 1.0f) {
                g_door[x][y] += 1.5f * fdt;
                if (g_door[x][y] > 1.0f)
                    g_door[x][y] = 1.0f;
            }
}

static void doom_update(u32 dt)
{
    if (dt > 100)
        dt = 100;

    if (g_state == ST_INTRO) {
        if (input_pressed(GKEY_ENTER) || input_pressed(GKEY_SPACE)) {
            klog("doom", "DEBUG start (enter pressed)");
            g_state = ST_PLAY;
        }
        return;
    }
    if (g_state == ST_DEAD || g_state == ST_WIN) {
        if (input_pressed(GKEY_ENTER)) {
            init_level();
            g_state = ST_PLAY;
        }
        return;
    }

    update_player(dt);
    update_enemies(dt);
    update_projectiles(dt);
    update_pickups_and_exit();
    update_doors(dt);

    if (g_hp <= 0) {
        g_hp = 0;
        g_state = ST_DEAD;
        klibc.snprintf(g_msg, sizeof(g_msg), "YOU DIED  (ENTER to retry)");
        g_msg_t = 0;
    }
}

/* --- rendering ------------------------------------------------------------- */

static u32 *g_row_tmp;   /* upscale row scratch (pmm) */

static void blit_scene(void)
{
    u32 y;

    for (y = 0; y < g_ih; y++) {
        u32 x;
        u32 *src = &g_buf[y * g_iw];
        u32 *dst = g_row_tmp;

        for (x = 0; x < g_iw; x++) {
            u32 c = src[x];
            dst[x * 2] = c;
            dst[x * 2 + 1] = c;
        }
        /* pad any odd last column */
        for (x = g_iw * 2; x < g_screen_w; x++)
            dst[x] = 0;
        framebuffer_putpixels(0, y * 2, dst, g_screen_w);
        framebuffer_putpixels(0, y * 2 + 1, dst, g_screen_w);
    }
}

static void cast_floor_and_sky(void)
{
    float ray0x = g_px * 0.0f, ray0y = g_py * 0.0f;
    float dx = tcos(g_pa), dy = tsin(g_pa);
    float px = -tsin(g_pa) * PLANE_MAG, py = tcos(g_pa) * PLANE_MAG;
    float rayDirX0 = dx - px, rayDirY0 = dy - py;
    float rayDirX1 = dx + px, rayDirY1 = dy + py;
    float posZ = 0.5f * (float)g_ih;
    int y;

    (void)ray0x; (void)ray0y;

    for (y = (int)(g_ih / 2); y < (int)g_ih; y++) {
        float p = (float)(y - (int)(g_ih / 2));
        float rowDist, stepX, stepY, floorX, floorY;
        u32 *row = &g_buf[y * g_iw];
        u32 *crow = &g_buf[(int)(g_ih - 1 - y) * g_iw];
        int s, x;
        u32 sky = g_sky[g_ih - 1 - y];

        if (p <= 0.0f)
            continue;
        rowDist = posZ / p;
        stepX = rowDist * (rayDirX1 - rayDirX0) / (float)g_iw;
        stepY = rowDist * (rayDirY1 - rayDirY0) / (float)g_iw;
        floorX = g_px + rowDist * rayDirX0;
        floorY = g_py + rowDist * rayDirY0;

        s = (int)(300.0f / rowDist);
        if (s > 255) s = 255;
        if (s < 40) s = 40;

        for (x = 0; x < (int)g_iw; x++) {
            int cx = (int)floorX, cy = (int)floorY;
            int chk = ((cx & 7) ^ (cy & 7)) & 4;
            u32 c = chk ? g_shade_dark[s] : g_shade_light[s];

            if (cx == g_exitx && cy == g_exity)
                c = shade_color(GUK_RGB(0x18, 0x88, 0x30), s);
            row[x] = c;
            crow[x] = sky;
            floorX += stepX;
            floorY += stepY;
        }
    }
}

static void cast_walls(void)
{
    float dx = tcos(g_pa), dy = tsin(g_pa);
    float px = -tsin(g_pa) * PLANE_MAG, py = tcos(g_pa) * PLANE_MAG;
    int x;

    for (x = 0; x < (int)g_iw; x++) {
        float cameraX = 2.0f * (float)x / (float)g_iw - 1.0f;
        float rdx = dx + px * cameraX, rdy = dy + py * cameraX;
        int mapX = (int)g_px, mapY = (int)g_py;
        float deltaX = rdx != 0.0f ? f_abs(1.0f / rdx) : 1e30f;
        float deltaY = rdy != 0.0f ? f_abs(1.0f / rdy) : 1e30f;
        float sideDistX, sideDistY;
        int stepX, stepY, side, hit = 0;
        int texX, wall;
        float perp, wallX;
        int lineH, drawStart, drawEnd, y;
        u32 trow[64];
        float step, texPos;

        if (rdx < 0.0f) {
            stepX = -1;
            sideDistX = (g_px - (float)mapX) * deltaX;
        } else {
            stepX = 1;
            sideDistX = ((float)mapX + 1.0f - g_px) * deltaX;
        }
        if (rdy < 0.0f) {
            stepY = -1;
            sideDistY = (g_py - (float)mapY) * deltaY;
        } else {
            stepY = 1;
            sideDistY = ((float)mapY + 1.0f - g_py) * deltaY;
        }

        while (!hit) {
            if (sideDistX < sideDistY) {
                sideDistX += deltaX;
                mapX += stepX;
                side = 0;
            } else {
                sideDistY += deltaY;
                mapY += stepY;
                side = 1;
            }
            if (mapX < 0 || mapY < 0 || mapX >= MAP_W || mapY >= MAP_H ||
                cell_is_solid(mapX, mapY))
                hit = 1;
        }

        perp = side == 0 ? sideDistX - deltaX : sideDistY - deltaY;
        if (perp < 0.01f)
            perp = 0.01f;

        lineH = (int)((float)g_ih / perp);
        drawStart = (int)(g_ih / 2) - lineH / 2;
        drawEnd = (int)(g_ih / 2) + lineH / 2;
        if (drawStart < 0) drawStart = 0;
        if (drawEnd > (int)g_ih) drawEnd = (int)g_ih;

        wall = g_wallmap[mapX][mapY] ? g_wallmap[mapX][mapY] : T_BRICK;
        if (wall == T_DOOR && g_door[mapX][mapY] >= 0.6f)
            wall = T_BRICK;   /* open doors read as their frame */

        if (side == 0)
            wallX = g_py + perp * rdy;
        else
            wallX = g_px + perp * rdx;
        wallX -= (float)(int)wallX;
        texX = (int)(wallX * 64.0f);
        if (side == 1)
            texX = 63 - texX;
        if (texX < 0) texX = 0;
        if (texX > 63) texX = 63;

        {
            u32 *tt = &g_tex[wall][texX];
            int s = (int)(280.0f / perp);
            if (s > 255) s = 255;
            if (s < 60) s = 60;
            for (y = 0; y < 64; y++)
                trow[y] = shade_color(tt[y * 64], s);
        }

        step = 64.0f / (float)lineH;
        texPos = (float)(drawStart - (int)(g_ih / 2) + lineH / 2) * step;
        for (y = drawStart; y < drawEnd; y++) {
            int ty = (int)texPos & 63;
            texPos += step;
            g_buf[y * g_iw + x] = trow[ty];
        }

        g_zbuf[x] = perp;
    }
}

static void draw_sprite(float sx, float sy, int height, const u8 *spr,
                        u32 stride, u32 wpx, u32 hpx,
                        const u32 *pal, int maxidx)
{
    int sh = height;
    int drawStartY = (int)(g_ih / 2) - sh / 2;
    int drawEndY = drawStartY + sh;
    int centerX = (int)(g_iw * 0.5f * (1.0f + sx / sy));
    int halfW = (int)((float)sh * (float)wpx / (float)hpx / 2.0f);
    int startX = centerX - halfW, endX = centerX + halfW;
    int x, y;

    if (sy < 0.1f)
        return;

    if (startX < 0) startX = 0;
    if (endX > (int)g_iw) endX = (int)g_iw;
    if (drawStartY < 0) drawStartY = 0;
    if (drawEndY > (int)g_ih) drawEndY = (int)g_ih;

    for (x = startX; x < endX; x++) {
        int tx = (int)((float)(x - centerX + halfW) * (float)wpx /
                       (float)(halfW * 2));
        if (tx < 0) tx = 0;
        if (tx >= (int)wpx) tx = (int)wpx - 1;

        for (y = drawStartY; y < drawEndY; y++) {
            int ty = (int)((float)(y - drawStartY) * (float)hpx /
                           (float)(sh > 0 ? sh : 1));
            int idx;
            u32 c;

            if (ty < 0 || ty >= (int)hpx)
                continue;
            idx = spr[ty * stride + tx];
            if (idx == 0 || idx >= maxidx)
                continue;
            c = pal[idx];
            if (sy < g_zbuf[x])
                g_buf[y * g_iw + x] = c;
        }
    }
}

static void cast_sprites(void)
{
    float dx = tcos(g_pa), dy = tsin(g_pa);
    float px = -tsin(g_pa) * PLANE_MAG, py = tcos(g_pa) * PLANE_MAG;
    float invDet = 1.0f / (px * dy - dx * py);
    int i;

    /* depth-sort entities (insertion, descending distance) */
    for (i = 1; i < g_ent_count; i++) {
        struct ent tmp = g_ents[i];
        int j = i - 1;
        float d_i = (tmp.x - g_px) * (tmp.x - g_px) +
                    (tmp.y - g_py) * (tmp.y - g_py);
        while (j >= 0) {
            float d_j = (g_ents[j].x - g_px) * (g_ents[j].x - g_px) +
                        (g_ents[j].y - g_py) * (g_ents[j].y - g_py);
            if (d_j >= d_i)
                break;
            g_ents[j + 1] = g_ents[j];
            j--;
        }
        g_ents[j + 1] = tmp;
    }

    for (i = 0; i < g_ent_count; i++) {
        struct ent *e = &g_ents[i];
        float rx, ry, tx, ty, height;
        int shade;

        if (!e->alive)
            continue;
        rx = e->x - g_px;
        ry = e->y - g_py;
        tx = invDet * (dy * rx - dx * ry);
        ty = invDet * (-py * rx + px * ry);
        if (ty < 0.1f)
            continue;

        height = (int)f_abs((float)g_ih / ty);
        shade = (int)(255.0f / (1.0f + ty * 0.22f));
        if (shade < 60) shade = 60;

        if (e->type == E_IMP) {
            u32 pal[5];
            int k;
            for (k = 0; k < 5; k++)
                pal[k] = shade_color(s_imp_pal[k], shade);
            draw_sprite(tx, ty, height, (const u8 *)g_imp_spr,
                        SPR_IMP_W, SPR_IMP_W, SPR_IMP_H, pal, 5);
        } else {
            const u8 *spr;
            const u32 *pal;
            int maxidx;

            if (e->type == E_HEALTH) {
                spr = (const u8 *)g_health_spr;
                pal = s_health_pal;
                maxidx = 3;
            } else {
                spr = (const u8 *)g_ammo_spr;
                pal = s_ammo_pal;
                maxidx = 3;
            }
            draw_sprite(tx, ty, height, spr,
                        SPR_ITEM_W, SPR_ITEM_W, SPR_ITEM_H, pal, maxidx);
        }
    }

    /* fireballs */
    for (i = 0; i < MAX_PROJ; i++) {
        struct proj *p = &g_proj[i];
        float rx, ry, tx, ty, height;

        if (!p->alive)
            continue;
        rx = p->x - g_px;
        ry = p->y - g_py;
        tx = invDet * (dy * rx - dx * ry);
        ty = invDet * (-py * rx + px * ry);
        if (ty < 0.1f)
            continue;
        height = (int)f_abs((float)g_ih / ty);
        draw_sprite(tx, ty, height, (const u8 *)g_ball_spr,
                    SPR_ITEM_W, SPR_ITEM_W, SPR_ITEM_H, s_ball_pal, 2);
    }
}

static void draw_weapon(void)
{
    u32 w = g_screen_w, h = g_screen_h;
    int cx = (int)(w / 2);
    int base = (int)h - HUD_H;
    int len = (int)(h * 0.26f);
    int bob = (int)(tsin((float)g_bob * 0.5f) * 4.0f);
    int rec = g_recoil * 2;
    int gy = base - len + rec;

    (void)bob;
    /* barrel */
    gfx_rect(cx - 4, gy + bob, 8, len - 12, GUK_RGB(0x5A, 0x5A, 0x60));
    gfx_rect(cx - 6, gy - 8 + bob, 12, 10, GUK_RGB(0x3A, 0x3A, 0x40));
    /* receiver */
    gfx_rect(cx - 14, base - 52 + rec, 28, 52, GUK_RGB(0x2E, 0x2E, 0x34));
    gfx_rect(cx - 18, base - 34 + rec, 36, 20, GUK_RGB(0x24, 0x24, 0x2A));
    /* grip */
    gfx_rect(cx - 6, base - 20 + rec, 12, 20, GUK_RGB(0x18, 0x18, 0x1E));
    /* highlight line */
    gfx_rect(cx - 3, gy + bob, 2, len - 12, GUK_RGB(0x8A, 0x8A, 0x92));
    /* muzzle flash */
    if (g_flash > 0) {
        gfx_rect(cx - 6, gy - 26 + bob, 12, 20, GUK_RGB(0xFF, 0xD2, 0x4A));
        gfx_rect(cx - 3, gy - 32 + bob, 6, 26, GUK_RGB(0xFF, 0x9F, 0x0A));
    }
}

static void draw_crosshair(void)
{
    int cx = (int)(g_screen_w / 2);
    int cy = (int)((g_screen_h - HUD_H) / 2);
    u32 c = GUK_RGB(0x28, 0xC8, 0x40);

    gfx_rect(cx - 6, cy, 12, 2, c);
    gfx_rect(cx - 1, cy - 5, 2, 12, c);
}

static void draw_hud(void)
{
    u32 w = g_screen_w, h = g_screen_h;
    int y0 = (int)h - HUD_H;
    char buf[48];
    u32 hpcol;

    gfx_rect(0, y0, w, HUD_H, GUK_RGB(0x0B, 0x0B, 0x0D));
    gfx_rect(0, y0, w, 2, GUK_RGB(0x2A, 0x2A, 0x30));

    /* health */
    gfx_text(16, y0 + 8, "H", GUK_RED, GUK_RGB(0x0B, 0x0B, 0x0D));
    klibc.snprintf(buf, sizeof(buf), "%d", g_hp);
    gfx_text(30, y0 + 8, buf, GUK_WHITE, GUK_RGB(0x0B, 0x0B, 0x0D));
    hpcol = g_hp > 60 ? GUK_GREEN : (g_hp > 30 ? GUK_YELLOW : GUK_RED);
    gfx_rect(16, y0 + 22, 96, 8, GUK_RGB(0x22, 0x22, 0x26));
    gfx_rect(16, y0 + 22, (u32)((96 * g_hp) / MAX_HP), 8, hpcol);

    /* ammo */
    gfx_text(140, y0 + 8, "AMMO", GUK_GRAY, GUK_RGB(0x0B, 0x0B, 0x0D));
    klibc.snprintf(buf, sizeof(buf), "%d", g_ammo);
    gfx_text(196, y0 + 8, buf, GUK_WHITE, GUK_RGB(0x0B, 0x0B, 0x0D));

    /* level / kills */
    klibc.snprintf(buf, sizeof(buf), "LVL 1   KILLS %d/%d",
                   g_imp_kills, g_imp_total);
    gfx_text((s32)((w - gfx_text_width(buf)) / 2), y0 + 8, buf,
             GUK_WHITE, GUK_RGB(0x0B, 0x0B, 0x0D));

    gfx_text((s32)w - 96, y0 + 24, "ESC QUIT", GUK_GRAY,
             GUK_RGB(0x0B, 0x0B, 0x0D));
    (void)h;
}

static void draw_message(void)
{
    u32 w = g_screen_w;

    if (g_msg[0] == '\0')
        return;
    gfx_rect((s32)((w - gfx_text_width(g_msg)) / 2) - 8, 14,
             gfx_text_width(g_msg) + 16, 22, GUK_RGB(0x0A, 0x0A, 0x0C));
    gfx_text((s32)((w - gfx_text_width(g_msg)) / 2), 20, g_msg,
             GUK_WHITE, GUK_RGB(0x0A, 0x0A, 0x0C));
}

static void draw_minimap(void)
{
    int x, y, px0 = 12, py0 = 12;
    int cs = 5;

    gfx_rect(px0 - 4, py0 - 4, MAP_W * cs + 8, MAP_H * cs + 8,
             GUK_RGB(0x10, 0x10, 0x14));

    for (y = 0; y < MAP_H; y++)
        for (x = 0; x < MAP_W; x++) {
            if (g_wallmap[x][y])
                gfx_rect(px0 + x * cs, py0 + y * cs, cs, cs,
                         GUK_RGB(0x8A, 0x8A, 0x92));
        }
    for (y = 0; y < MAP_H; y++)
        for (x = 0; x < MAP_W; x++)
            if (g_wallmap[x][y] == T_DOOR && g_door[x][y] > 0.0f)
                gfx_rect(px0 + x * cs, py0 + y * cs, cs, cs,
                         GUK_RGB(0xFF, 0xD2, 0x4A));

    for (x = 0; x < g_ent_count; x++) {
        struct ent *e = &g_ents[x];
        if (!e->alive)
            continue;
        gfx_rect(px0 + (int)(e->x * cs) - 1, py0 + (int)(e->y * cs) - 1,
                 3, 3, e->type == E_IMP ? GUK_RED : GUK_CYAN);
    }
    /* exit */
    gfx_rect(px0 + g_exitx * cs - 1, py0 + g_exity * cs - 1, 3, 3, GUK_GREEN);

    /* player + facing */
    gfx_rect(px0 + (int)(g_px * cs) - 2, py0 + (int)(g_py * cs) - 2, 5, 5,
             GUK_WHITE);
    gfx_line(px0 + (int)(g_px * cs), py0 + (int)(g_py * cs),
             px0 + (int)((g_px + tcos(g_pa) * 1.2f) * cs),
             py0 + (int)((g_py + tsin(g_pa) * 1.2f) * cs), GUK_WHITE);
}

static void draw_intro(void)
{
    u32 w = g_screen_w, h = g_screen_h;

    gfx_clear(GUK_BLACK);
    gfx_rect((s32)((w - 320) / 2), (s32)(h / 2 - 110), 320, 3, GUK_RED);
    gfx_text((s32)((w - gfx_text_width("XKERN DOOM")) / 2),
             (s32)(h / 2 - 84), "XKERN DOOM", GUK_RED, GUK_BLACK);
    gfx_text((s32)((w - gfx_text_width("a first-person raycaster")) / 2),
             (s32)(h / 2 - 60), "a first-person raycaster", GUK_GRAY, GUK_BLACK);
    gfx_text((s32)((w - gfx_text_width("W/S move   A/D strafe   ARROWS turn")) / 2),
             (s32)(h / 2 - 20), "W/S move   A/D strafe   ARROWS turn",
             GUK_WHITE, GUK_BLACK);
    gfx_text((s32)((w - gfx_text_width("MOUSE turn+fire   ENTER fire   SPACE door")) / 2),
             (s32)(h / 2 + 4), "MOUSE turn+fire   ENTER fire   SPACE door",
             GUK_WHITE, GUK_BLACK);
    gfx_text((s32)((w - gfx_text_width("M minimap   kill the imps, reach the exit")) / 2),
             (s32)(h / 2 + 28), "M minimap   kill the imps, reach the exit",
             GUK_WHITE, GUK_BLACK);
    gfx_text((s32)((w - gfx_text_width("Press ENTER to start")) / 2),
             (s32)(h / 2 + 72), "Press ENTER to start", GUK_YELLOW, GUK_BLACK);
}

static void draw_end_screen(int won)
{
    u32 w = g_screen_w, h = g_screen_h;
    const char *a = won ? "LEVEL COMPLETE" : "YOU DIED";
    const char *b = won ? "You purged the base." : "The imps got you.";

    gfx_clear(GUK_BLACK);
    gfx_text((s32)((w - gfx_text_width(a)) / 2), (s32)(h / 2 - 20),
             a, won ? GUK_GREEN : GUK_RED, GUK_BLACK);
    gfx_text((s32)((w - gfx_text_width(b)) / 2), (s32)(h / 2 + 8),
             b, GUK_GRAY, GUK_BLACK);
    gfx_text((s32)((w - gfx_text_width("ENTER to retry   ESC to quit")) / 2),
             (s32)(h / 2 + 44), "ENTER to retry   ESC to quit",
             GUK_WHITE, GUK_BLACK);
}

static void doom_render(void)
{
    static int dbg_first = 1;
    if (dbg_first) {
        klog("doom", "DEBUG first render %ux%u state=%d", g_screen_w, g_screen_h, g_state);
        dbg_first = 0;
    }
    if (g_state == ST_INTRO) {
        draw_intro();
        return;
    }
    if (g_state == ST_DEAD || g_state == ST_WIN) {
        draw_end_screen(g_state == ST_WIN);
        return;
    }

    cast_floor_and_sky();
    cast_walls();
    cast_sprites();

    blit_scene();

    draw_weapon();
    draw_crosshair();
    draw_message();
    if (g_minimap)
        draw_minimap();

    if (g_hurt > 0) {
        u32 w = g_screen_w, h = g_screen_h;
        gfx_rect(0, 0, w, 10, GUK_RED);
        gfx_rect(0, (s32)h - 10, w, 10, GUK_RED);
        gfx_rect(0, 0, 10, h, GUK_RED);
        gfx_rect((s32)w - 10, 0, 10, h, GUK_RED);
    }

    draw_hud();
}

/* --- init ------------------------------------------------------------------ */

static void doom_init(void)
{
    u32 pages;
    int i;

    g_screen_w = gfx_width();
    g_screen_h = gfx_height();
    g_iw = g_screen_w / 2;
    g_ih = g_screen_h / 2;
    if (g_iw > 800) g_iw = 800;
    if (g_ih > 600) g_ih = 600;

    build_trig();
    build_textures();
    build_sprites();
    build_shade_tables();

    pages = ((u32)g_iw * g_ih * 4 + PAGE_SIZE - 1) >> 12;
    if (pages && !g_buf) {
        g_buf = 0;
        g_buf_pages = pages;
        for (i = 0; i < (int)pages; i++) {
            u32 page = pmm_alloc();
            if (!page)
                break;
            paging_map_region(page, page, PAGE_SIZE, PAGE_WRITE);
            if (i == 0)
                g_buf = (u32 *)page;
        }
    }
    pages = ((u32)g_iw * 4 + PAGE_SIZE - 1) >> 12;
    if (pages && !g_zbuf) {
        g_zbuf_pages = pages;
        for (i = 0; i < (int)pages; i++) {
            u32 page = pmm_alloc();
            if (!page)
                break;
            paging_map_region(page, page, PAGE_SIZE, PAGE_WRITE);
            if (i == 0)
                g_zbuf = (float *)page;
        }
    }
    pages = ((u32)g_screen_w * 4 + PAGE_SIZE - 1) >> 12;
    if (pages && !g_row_tmp) {
        for (i = 0; i < (int)pages; i++) {
            u32 page = pmm_alloc();
            if (!page)
                break;
            paging_map_region(page, page, PAGE_SIZE, PAGE_WRITE);
            if (i == 0)
                g_row_tmp = (u32 *)page;
        }
    }

    if (!g_buf || !g_zbuf || !g_row_tmp) {
        klog("doom", "render buffer allocation failed");
        return;
    }

    for (i = 0; i < (int)g_ih; i++)
        g_sky[i] = sky_row((u32)i);

    init_level();
    g_state = ST_INTRO;

    klog("doom", "buffer %ux%u, %u pages", g_iw, g_ih, g_buf_pages);
}

/* --- run ------------------------------------------------------------------- */

void doom_run(void)
{
    static struct game game = {
        doom_init,
        doom_update,
        doom_render,
        0,
        0,
    };

    engine_init("XKERN DOOM");
    engine_run(&game);
    klog("doom", "DEBUG engine_run returned (ESC)");
    input_clear();
}
