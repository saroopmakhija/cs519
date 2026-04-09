#include<stdlib.h>

typedef struct caslock_t {
    volatile unsigned int lock;
} caslock_t;

caslock_t* caslock_create(){
    caslock_t* lock= (caslock_t*)malloc(sizeof(caslock_t));
    return lock;
}


void caslock_init(caslock_t *lock)
{
  lock->lock = 0;
}

void caslock_destroy(caslock_t *lock)
{
  lock->lock == 0;
  free(lock);
}

void caslock_unlock(caslock_t *lock)
{
  lock->lock = 0;
}

void caslock_lock(caslock_t *lock)
{
    while(__sync_val_compare_and_swap(&(lock->lock), 0 , 1)){}
}