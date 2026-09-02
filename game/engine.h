#ifndef GAME_ENGINE_H
#define GAME_ENGINE_H

#include "types.h"

#define GAME_FPS     60
#define GAME_TICK_MS (1000 / GAME_FPS)

struct game {
    void (*init)(void);
    void (*update)(u32 dt_ms);
    void (*render)(void);
    u32  run_ms;   /* 0 = run until the game quits or ESC is pressed */
    int  quit;
};

void engine_init(const char *title);
void engine_run(struct game *g);
void engine_quit(void);

u32 engine_ms(void);
u32 engine_frame(void);
void engine_sleep(u32 ms);

#endif
