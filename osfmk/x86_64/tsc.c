#include "tsc.h"
#include "io.h"
#include "klog.h"

#define PIT_CH0       0x40
#define PIT_CMD       0x43
#define PIT_BASE_FREQ 1193182

static uint64_t g_tsc_freq;

static inline uint64_t tsc_rdtsc_raw(void) {
    uint32_t lo, hi;

    asm volatile ("rdtsc" : "=a"(lo), "=d"(hi));
    return ((uint64_t)hi << 32) | lo;
}

static inline uint16_t pit_read_count(void) {
    uint16_t count;

    outb(PIT_CMD, 0x00);
    count = (uint16_t)inb(PIT_CH0);
    count |= (uint16_t)inb(PIT_CH0) << 8;
    return count;
}

void tsc_init(void) {
    tsc_calibrate();
    klog("tsc", "tsc_init() %u ghz", tsc_ghz());
}

/*
 * Freeze the time base: every reader (tsc_ms/us/ns) returns 0 from now
 * on.  Used by panic() so late log lines carry no bogus timestamps.
 */
void tsc_disable(void) {
    g_tsc_freq = 0;
}

void tsc_calibrate(void) {
    uint64_t start_tsc, end_tsc;
    uint16_t start_count;

    outb(PIT_CMD, 0x30);
    outb(PIT_CH0, 0xFF);
    outb(PIT_CH0, 0xFF);

    do {
        start_count = pit_read_count();
    } while (start_count > 0xFFF0);

    start_tsc = tsc_rdtsc_raw();

    while (pit_read_count() != 0) {
        asm volatile ("pause");
    }

    end_tsc = tsc_rdtsc_raw();

    g_tsc_freq = (end_tsc - start_tsc) * PIT_BASE_FREQ / start_count;
}

uint64_t tsc_rdtsc(void) {
    return tsc_rdtsc_raw();
}

uint32_t tsc_mhz(void) {
    return (uint32_t)(g_tsc_freq / 1000000);
}

uint32_t tsc_ghz(void) {
    return (uint32_t)(g_tsc_freq / 1000000000);
}

uint64_t tsc_us(void) {
    if (!g_tsc_freq) return 0;
    return tsc_rdtsc_raw() / (g_tsc_freq / 1000000);
}

uint64_t tsc_ms(void) {
    if (!g_tsc_freq) return 0;
    return tsc_rdtsc_raw() / (g_tsc_freq / 1000);
}

uint64_t tsc_ns(void) {
    if (!g_tsc_freq) return 0;
    return tsc_rdtsc_raw() * 1000000000ull / g_tsc_freq;
}

uint64_t tsc_task_begin(void) {
    return tsc_rdtsc_raw();
}

void tsc_task_end(uint64_t start, const char *driver, const char *task) {
    uint64_t end = tsc_rdtsc_raw();
    uint64_t elapsed_ms = (end - start) / (g_tsc_freq / 1000);
    klog(driver, "%s took %u ms", task, (uint32_t)elapsed_ms);
}
