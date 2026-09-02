/**
 * background_tasks.c - deferred work queue for the boot sequence.
 *
 *  Lightweight ring-buffer of function pointers that are drained once
 *  the plymouth splash is dismissed.  Tasks are enqueued during early
 *  init (HW probing, timer setup, ...) and executed sequentially.
 *  xkern 26.0.8
 */

#include "background_tasks.h"
#include "klog.h"

/* ===================================================================== */
/*  Task ring buffer                                                      */
/* ===================================================================== */

#define BG_MAX_TASKS 32

typedef void (*bg_task_fn)(void);

static bg_task_fn g_queue[BG_MAX_TASKS];
static int         g_head;
static int         g_tail;
static int         g_count;

/* ===================================================================== */
/*  API                                                                   */
/* ===================================================================== */

void bg_tasks_init(void)
{
    g_head  = 0;
    g_tail  = 0;
    g_count = 0;
    klog("boot.bg", "background task queue initialised (cap %d)", BG_MAX_TASKS);
}

int bg_tasks_enqueue(bg_task_fn fn)
{
    if (g_count >= BG_MAX_TASKS) {
        klog("boot.bg", "task queue full, dropping task");
        return -1;
    }
    g_queue[g_tail] = fn;
    g_tail = (g_tail + 1) % BG_MAX_TASKS;
    g_count++;
    return 0;
}

void bg_tasks_run_pending(void)
{
    int ran = 0;

    while (g_count > 0) {
        bg_task_fn fn = g_queue[g_head];
        g_head = (g_head + 1) % BG_MAX_TASKS;
        g_count--;

        if (fn) {
            fn();
            ran++;
        }
    }

    klog("boot.bg", "ran %d background task(s)", ran);
}

int bg_tasks_pending(void)
{
    return g_count;
}
