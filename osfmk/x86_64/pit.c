#include "pit.h"
#include "io.h"
#include "idt.h"
#include "pic.h"
#include "klog.h"
#include "IOUSBFamily/hid/usbhid.h"
#include "gpukit/lv_console.h"
#include "IONetFamily/ionet.h"

static volatile uint32_t g_pit_ticks;
static uint32_t g_pit_hz;

void pit_set_freq(uint32_t hz) {
    uint32_t divisor = PIT_BASE_FREQ / hz;

    outb(PIT_CMD, 0x36);
    outb(PIT_CH0, divisor & 0xFF);
    outb(PIT_CH0, (divisor >> 8) & 0xFF);

    g_pit_hz = hz;
}

void pit_init(uint32_t hz) {
    g_pit_ticks = 0;
    pit_set_freq(hz);
    klog("pit", "pit_init() %u hz", hz);
}

uint32_t pit_get_ticks(void) {
    return g_pit_ticks;
}

uint32_t pit_ms_to_ticks(uint32_t ms) {
    return (ms * g_pit_hz) / 1000;
}

void pit_sleep(uint32_t ms) {
    uint32_t start = g_pit_ticks;
    uint32_t ticks = pit_ms_to_ticks(ms);

    while ((g_pit_ticks - start) < ticks) {
        asm volatile ("pause");
    }
}

void pit_register_irq(void) {
    idt_set_gate(0x20, (uint32_t)irq0_handler, 0x08, 0x8E);
    pic_enable_irq(0); //ln 41 irq data
}

void pit_handler_irq0(void) {
    g_pit_ticks++;
    usb_check_event();
    /* drain NIC rx queues + run DHCP/ping state machines */
    ionet_poll();
    /* keep LVGL console animations advancing + flush pending text */
    if (lv_console_active())
        lv_console_pump();
    pic_send_eoi(0);
}
