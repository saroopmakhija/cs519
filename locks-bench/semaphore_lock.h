#include <semaphore.h>
#include <stdlib.h>


sem_t* semaphore_create(){
    sem_t* lock = (sem_t*)malloc(sizeof(sem_t));
    return lock;
}


void semaphore_init(sem_t *lock)
{
  sem_init(lock, 0, 1);
}

void semaphore_destroy(sem_t *lock)
{
  sem_destroy(lock);
}

void semaphore_unlock(sem_t *lock)
{
  sem_post(lock); 
}

void semaphore_lock(sem_t *lock)
{
  sem_wait(lock);
}