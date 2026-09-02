#ifndef SPINLOCK_H
#define SPINLOCK_H

#include "types.h"

typedef struct {
    volatile u32 locked;
} spinlock_t;

#define SPINLOCK_INIT { 0 }

void spinlock_acquire(spinlock_t *lock);
void spinlock_release(spinlock_t *lock);
int  spinlock_try(spinlock_t *lock);

#endif
