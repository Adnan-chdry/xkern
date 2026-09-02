#include "spinlock.h"

void spinlock_acquire(spinlock_t *lock)
{
    while (__sync_lock_test_and_set(&lock->locked, 1))
        asm volatile("pause");
}

void spinlock_release(spinlock_t *lock)
{
    __sync_lock_release(&lock->locked);
}

int spinlock_try(spinlock_t *lock)
{
    return __sync_lock_test_and_set(&lock->locked, 1) == 0;
}
