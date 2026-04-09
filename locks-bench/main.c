#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <sys/time.h>
#include <assert.h>

#include "locks.h"

#define NUM_COUNTERS 16
#define DEFAULT_THREADS 16
#define DEFAULT_ITERATIONS 10000
#define DEFAULT_WORK_LENGTH 1000

int num_counters = NUM_COUNTERS;
int num_threads;
int iterations;
int work_length;
int *counters;
lock_t *lock;
static struct timeval start_time;
static struct timeval end_time;

void do_work(int counter_id) {
    for (int j = 0; j < work_length; j++) {
        counters[counter_id]++;
    }
}

void *inc_thread(void *id) {
    int thread_id = (int)(long)id;
    int counter_id = thread_id % num_counters;

    for (int i = 0; i < iterations; i++) {
        acquire_lock(lock);
        do_work(counter_id);
        release_lock(lock);
    }
}

static void calc_time(struct timeval *start, struct timeval *end) {
    if (end->tv_usec < start->tv_usec) {
        end->tv_sec -= 1;
        end->tv_usec += 1000000;
    }

    assert(end->tv_sec >= start->tv_sec);
    assert(end->tv_usec >= start->tv_usec);
    struct timeval interval = {
        end->tv_sec - start->tv_sec,
        end->tv_usec - start->tv_usec
    };
    printf("Total time %ld.%06ld seconds\t\n", (long)interval.tv_sec, (long)interval.tv_usec);
}

void print_usage(char *cmd) {
    printf("Usage: %s <lock_type> [num_threads] [iterations] [work_length]\n", cmd);
    printf("\n");
    printf("Lock Types:\n");
    printf("    1 - Spin lock: Compare-and-Swap\n");
    printf("    2 - Spin lock: Test-and-Set\n");
    printf("    3 - Spin lock: Ticket Lock\n");
    printf("    4 - Mutex\n");
    printf("    5 - Semaphore\n");
}

int main(int argc, char **argv) {
    pthread_t *thr;
    int ret = 0;

    if (argc < 2 || argc > 5) {
        print_usage(argv[0]);
        exit(1);
    }

    int lock_type = atoi(argv[1]);
    if (lock_type < 1 || lock_type > 5) {
        printf("Invalid lock type provided\n");
        exit(1);
    }

    num_threads = (argc > 2) ? atoi(argv[2]) : DEFAULT_THREADS;
    iterations = (argc > 3) ? atoi(argv[3]) : DEFAULT_ITERATIONS;
    work_length = (argc > 4) ? atoi(argv[4]) : DEFAULT_WORK_LENGTH;

    printf("Using %d threads, %d counters, %d iterations, and work length of %d\n", num_threads, num_counters, iterations, work_length);
    thr = calloc(sizeof(*thr), num_threads);
    lock = create_lock(lock_type);
    counters = (int*)malloc(sizeof(int) * num_counters);
    init_lock(lock);

    gettimeofday(&start_time, NULL);

    // Start threads
    for (long i = 0; i < num_threads; i++) {
        if (pthread_create(&thr[i], NULL, inc_thread, (void *)i) != 0) {
            perror("Thread creation failed");
        }
    }

    // Join threads
    for (long i = 0; i < num_threads; i++)
        pthread_join(thr[i], NULL);

    gettimeofday(&end_time, NULL);

    int expected_value = (num_threads / num_counters) * iterations * work_length;
    for (int i = 0; i < num_counters; i++) {
        printf("Counter %d value: %d, expected: %d\n", i, counters[i], expected_value);
    }
    calc_time(&start_time, &end_time);

    free(counters);
    destroy_lock(lock);

    return ret;
}
