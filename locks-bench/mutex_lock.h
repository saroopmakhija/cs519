#include<pthread.h>
#include<stdlib.h>

pthread_mutex_t* mutex_create(){
    pthread_mutex_t* lock = (pthread_mutex_t*)malloc(sizeof(pthread_mutex_t));
    return lock;
}


void mutex_init(pthread_mutex_t *lock)
{
  pthread_mutex_init(lock, NULL);
}

void mutex_destroy(pthread_mutex_t *lock)
{
  pthread_mutex_destroy(lock);
}

void mutex_unlock(pthread_mutex_t *lock)
{
  pthread_mutex_unlock(lock); 
}

void mutex_lock(pthread_mutex_t *lock)
{
    pthread_mutex_lock(lock);
}
