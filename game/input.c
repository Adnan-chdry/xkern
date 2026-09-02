/*
 * input.c - keyboard input for the xkern game engine.
 *
 * Input is polled rather than interrupt-driven.  atkbd_poll() drains
 * whatever scancodes the PS/2 controller has buffered and updates the
 * key state table + character ring used by atkbd_is_key_pressed() and
 * atkbd_getchar().  usb_check_event() does the same for any USB
 * keyboard.  Both are safe to call with interrupts disabled, which is
 * how the engine runs.
 */
#include "input.h"
#include "atkbd.h"
#include "atmouse.h"
#include "usbhid.h"
#include "klog.h"

static u8 g_prev[128];
static u8 g_just[128];
static struct input_mouse g_mouse;

void input_poll(void)
{
    u32 i;

    /* The PS/2 controller has a single output buffer shared by both
     * devices, so drain the mouse first to keep its bytes from being
     * misread as keyboard scancodes. */
    atmouse_poll();
    atkbd_poll();
    extern void usb_check_event(void);
    usb_check_event();

    for (i = 0; i < 128; i++) {
        int now = atkbd_is_key_pressed((u8)i);

        g_just[i] = 0;
        if (now && !g_prev[i])
            g_just[i] = 1;
        g_prev[i] = (u8)now;
    }

    /* accumulate mouse deltas between frames.  Fold in the USB mouse too
     * so any pointer (PS/2 or USB HID) drives the same unified state. */
    atmouse_sample(&g_mouse.dx, &g_mouse.dy, &g_mouse.buttons);
    if (usb_mouse_present())
        usb_mouse_sample(&g_mouse.dx, &g_mouse.dy, &g_mouse.buttons);
}

int input_key(u8 scancode)
{
    if (scancode >= 128)
        return 0;
    return atkbd_is_key_pressed(scancode);
}

int input_pressed(u8 scancode)
{
    if (scancode >= 128)
        return 0;
    return g_just[scancode];
}

int input_getchar(void)
{
    return atkbd_getchar();
}

int input_mouse(struct input_mouse *m)
{
    if (!atmouse_ready() && !usb_mouse_present())
        return 0;
    if (m)
        *m = g_mouse;
    /* consume the accumulated deltas: input_poll() adds into g_mouse every
     * frame, so if we don't clear it here the cursor position drifts to a
     * screen edge and sticks there after the first movement. */
    g_mouse.dx = 0;
    g_mouse.dy = 0;
    g_mouse.buttons = 0;
    return 1;
}

/* Any pointer available (PS/2 mouse or USB HID mouse)? */
int input_mouse_present(void)
{
    return atmouse_ready() || usb_mouse_present();
}

void input_clear(void)
{
    int i;

    for (i = 0; i < 128; i++) {
        g_prev[i] = 0;
        g_just[i] = 0;
    }

    while (atkbd_getchar() >= 0)
        ;

    klog("game.input", "input state cleared");
}
