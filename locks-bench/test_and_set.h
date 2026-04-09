#include<stdlib.h>

typedef struct taslock_t {
    volatile unsigned int lock;
} taslock_t;


taslock_t* taslock_create(){
    taslock_t* lock= (taslock_t*)malloc(sizeof(taslock_t));
    return lock;
}

void taslock_init(taslock_t *lock)
{
  lock->lock = 0;
}

void taslock_destroy(taslock_t *lock)
{
  lock->lock == 0;
  free(lock);
}

void taslock_unlock(taslock_t *lock)
{
  lock->lock = 0;
}

void taslock_lock(taslock_t *lock)
{
    while(__sync_lock_test_and_set(&(lock->lock), 1)){}
}