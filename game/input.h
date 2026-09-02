#ifndef GAME_INPUT_H
#define GAME_INPUT_H

#include "types.h"

/* PS/2 scancode set 1 (make codes) */
enum game_key {
    GKEY_ESC   = 0x01,
    GKEY_1     = 0x02,
    GKEY_2     = 0x03,
    GKEY_3     = 0x04,
    GKEY_4     = 0x05,
    GKEY_5     = 0x06,
    GKEY_Q     = 0x10,
    GKEY_W     = 0x11,
    GKEY_E     = 0x12,
    GKEY_R     = 0x13,
    GKEY_A     = 0x1E,
    GKEY_S     = 0x1F,
    GKEY_D     = 0x20,
    GKEY_F     = 0x21,
    GKEY_N     = 0x31,
    GKEY_M     = 0x32,
    GKEY_Z     = 0x2C,
    GKEY_X     = 0x2D,
    GKEY_C     = 0x2E,
    GKEY_ENTER = 0x1C,
    GKEY_SPACE = 0x39,
    GKEY_LEFT  = 0x4B,
    GKEY_RIGHT = 0x4D,
    GKEY_UP    = 0x48,
    GKEY_DOWN  = 0x50,
};

/* Poll hardware (PS/2 scancodes + USB events) and refresh input state.
 * Call once per frame. */
void input_poll(void);

/* Is the key currently held down? */
int input_key(u8 scancode);

/* Was the key pressed since the last input_poll()? */
int input_pressed(u8 scancode);

/* One queued ASCII character, or -1 if none pending. */
int input_getchar(void);

/* --- mouse --------------------------------------------------------------- */

struct input_mouse {
    int dx;          /* pixel deltas since last poll (+x right) */
    int dy;          /* (+y down, already flipped for screen space) */
    u8 buttons;      /* bit0 left, bit1 right, bit2 middle */
};

/* Current accumulated mouse state since the last input_poll().  Returns 1
 * when a PS/2 mouse is present and enabled. */
int input_mouse(struct input_mouse *m);

/* Any pointer available (PS/2 mouse or USB HID mouse)? */
int input_mouse_present(void);

void input_clear(void);

#endif
