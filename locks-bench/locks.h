#include "compare_and_swap.h"
#include "mutex_lock.h"
#include "semaphore_lock.h"
#include "test_and_set.h"
#include "ticket_lock.h"
#include<stdio.h>

typedef enum lock_types{
    COMPARE_AND_SWAP = 1,
    TEST_AND_SET,
    TICKET,
    MUTEX,
    SEMAPHORE,
} lock_types;


// wrapper type and methods for different lock implementations
typedef struct lock_t {
    int lock_type;
    void *lock;
} lock_t;


lock_t* create_lock(lock_types lock_type){
    lock_t *lock = (lock_t*)malloc(sizeof(lock_t));

    switch (lock_type)
    {
    case COMPARE_AND_SWAP:
        lock->lock_type = COMPARE_AND_SWAP;
        lock->lock = caslock_create();
        break;

    case MUTEX:
        lock->lock_type = MUTEX;
        lock->lock = mutex_create();
        break;
    
    case SEMAPHORE:
        lock->lock_type = SEMAPHORE;
        lock->lock = semaphore_create();
        break;

    case TEST_AND_SET:
        lock->lock_type = TEST_AND_SET;
        lock->lock = taslock_create();
        break;

    case TICKET:
        lock->lock_type = TICKET;
        lock->lock = ticket_create();
        break;

    default:
        printf("invalid lock type\n");
        free(lock);
        lock = NULL;
        break;
    }

    return lock;
}



void destroy_lock(lock_t *lock){
    switch(lock->lock_type){
        case COMPARE_AND_SWAP:
        caslock_destroy((caslock_t*)lock->lock);
        break;

        case MUTEX:
        mutex_destroy((pthread_mutex_t*)lock->lock);
        break;
        
        case SEMAPHORE:
        semaphore_destroy((sem_t*)lock->lock);
        break;
        
        case TEST_AND_SET:
        taslock_destroy((taslock_t*)lock->lock);
        break;
        
        case TICKET:
        ticket_destroy((ticket_lock_t*)lock->lock);
        break;
    }

    free(lock);
    lock=NULL;
}


void init_lock(lock_t *lock){
    switch(lock->lock_type){
        case COMPARE_AND_SWAP:
        caslock_init((caslock_t*)lock->lock);
        break;

        case MUTEX:
        mutex_init((pthread_mutex_t*)lock->lock);
        break;
        
        case SEMAPHORE:
        semaphore_init((sem_t*)lock->lock);
        break;
        
        case TEST_AND_SET:
        taslock_init((taslock_t*)lock->lock);
        break;
        
        case TICKET:
        ticket_init((ticket_lock_t*)lock->lock);
        break;
    }
}


void acquire_lock(lock_t *lock){
    switch(lock->lock_type){
        case COMPARE_AND_SWAP:
        caslock_lock((caslock_t*)lock->lock);
        break;

        case MUTEX:
        mutex_lock((pthread_mutex_t*)lock->lock);
        break;
        
        case SEMAPHORE:
        semaphore_lock((sem_t*)lock->lock);
        break;
        
        case TEST_AND_SET:
        taslock_lock((taslock_t*)lock->lock);
        break;
        
        case TICKET:
        ticket_acquire((ticket_lock_t*)lock->lock);
        break;
    }
}



void release_lock(lock_t *lock){
    switch(lock->lock_type){
        case COMPARE_AND_SWAP:
        caslock_unlock((caslock_t*)lock->lock);
        break;

        case MUTEX:
        mutex_unlock((pthread_mutex_t*)lock->lock);
        break;
        
        case SEMAPHORE:
        semaphore_unlock((sem_t*)lock->lock);
        break;
        
        case TEST_AND_SET:
        taslock_unlock((taslock_t*)lock->lock);
        break;
        
        case TICKET:
        ticket_release((ticket_lock_t*)lock->lock);
        break;
    }
}