#ifndef GAME_DOOM_H
#define GAME_DOOM_H

/* Runs a Doom-style first-person raycaster ("XKERN DOOM") with the game
 * engine.  Returns when the player presses ESC, so the kernel can continue
 * booting. */
void doom_run(void);

#endif
