/*
 * xkern 2D game engine
 *
 * A small, dependency-light 2D game engine that runs in the xkern
 * kernel.  It is built on top of the HWframe framebuffer driver
 * (double-buffered via drivers/HWframe/buffer.c), the PS/2 + USB
 * keyboard drivers and the PIT/TSC clock drivers.
 *
 * Modules:
 *   gfx     - 2D raster primitives (rects, lines, circles, sprites, text)
 *   input   - keyboard state / just-pressed edges / queued characters
 *   engine  - fixed-rate game loop, timing and game structure
 *   demo    - a playable breakout demo wired into the kernel boot
 *
 * The engine runs in kernel mode before the getty scheduler boots,
 * so it polls hardware directly instead of relying on IRQ handlers.
 * This is intentional and documented in engine.c.
 */
#ifndef GAME_H
#define GAME_H

#include "gfx.h"
#include "input.h"
#include "engine.h"

#endif
