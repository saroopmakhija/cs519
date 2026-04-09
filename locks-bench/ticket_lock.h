#include<stdlib.h>
#include <sched.h>

typedef struct ticket_lock_t {
    volatile unsigned short next_ticket;
    char padding[64];
    volatile unsigned short now_serving;
} ticket_lock_t;

ticket_lock_t* ticket_create(){
    ticket_lock_t* lock= (ticket_lock_t*)malloc(sizeof(ticket_lock_t));
    return lock;
}

void ticket_init (ticket_lock_t *lock) {
    lock->now_serving = 0;
    lock->next_ticket = 0;
}

void ticket_destroy (ticket_lock_t *lock) {
    lock->now_serving = 0;
    lock->next_ticket = 0;
    free(lock);
}

void ticket_acquire (ticket_lock_t *lock) {
    unsigned int my_ticket;

    my_ticket = __sync_fetch_and_add(&(lock->next_ticket), 1);
    while (lock->now_serving != my_ticket){
        sched_yield();
    }
}

void ticket_release (ticket_lock_t *lock) {
    lock->now_serving += 1;
}