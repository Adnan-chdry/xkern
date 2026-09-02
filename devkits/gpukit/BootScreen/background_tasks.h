#ifndef BACKGROUND_TASKS_H
#define BACKGROUND_TASKS_H

/*
 *  background_tasks.h - deferred work queue API.
 *  xkern 26.0.8
 */

/* Initialise the task queue (call once at boot). */
void bg_tasks_init(void);

/* Enqueue a void(void) callback.  Returns 0 on success, -1 if full. */
int  bg_tasks_enqueue(void (*fn)(void));

/* Execute all queued tasks (called after splash is dismissed). */
void bg_tasks_run_pending(void);

/* Number of tasks still waiting. */
int  bg_tasks_pending(void);

#endif /* BACKGROUND_TASKS_H */
