#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <unistd.h>
#include <sched.h>
#include <sys/syscall.h>
#include <errno.h>
#include <string.h>
#include <time.h>
#include <stdatomic.h>

#define SYS_coop_sched 449

static atomic_int stop_flag = 0;

struct worker_arg {
    int mode;
    int tid;
};

static long coop_sched_call(int inactive)
{
    return syscall(SYS_coop_sched, inactive);
}

static void dummy_work(volatile unsigned long long *x)
{
    *x = (*x * 1664525ULL + 1013904223ULL);
}

static void *worker(void *arg)
{
    struct worker_arg *wa = (struct worker_arg *)arg;
    volatile unsigned long long x = (unsigned long long)(wa->tid + 1);

    if (wa->mode == 2) {
        if (coop_sched_call(1) < 0) {
            fprintf(stderr, "worker %d: coop_sched(1) failed: %s\n",
                    wa->tid, strerror(errno));
        }
    }

    while (!atomic_load(&stop_flag)) {
        dummy_work(&x);

        if (wa->mode == 1) {
            sched_yield();
        }
    }

    if (wa->mode == 2) {
        if (coop_sched_call(0) < 0) {
            fprintf(stderr, "worker %d: coop_sched(0) failed: %s\n",
                    wa->tid, strerror(errno));
        }
    }

    return NULL;
}

static int parse_mode(const char *s)
{
    if (strcmp(s, "normal") == 0)
        return 0;
    if (strcmp(s, "yield") == 0)
        return 1;
    if (strcmp(s, "coop") == 0)
        return 2;

    fprintf(stderr, "Unknown mode: %s\n", s);
    fprintf(stderr, "Usage: ./spin_bench <threads> <seconds> <normal|yield|coop>\n");
    exit(1);
}

int main(int argc, char **argv)
{
    int threads;
    int seconds;
    int mode;
    pthread_t *tids;
    struct worker_arg *args;

    if (argc != 4) {
        fprintf(stderr, "Usage: %s <threads> <seconds> <normal|yield|coop>\n", argv[0]);
        return 1;
    }

    threads = atoi(argv[1]);
    seconds = atoi(argv[2]);
    mode = parse_mode(argv[3]);

    if (threads <= 0 || seconds <= 0) {
        fprintf(stderr, "threads and seconds must be positive\n");
        return 1;
    }

    tids = calloc((size_t)threads, sizeof(pthread_t));
    args = calloc((size_t)threads, sizeof(struct worker_arg));
    if (!tids || !args) {
        perror("calloc");
        return 1;
    }

    printf("spin_bench: threads=%d seconds=%d mode=%s\n",
           threads, seconds, argv[3]);

    for (int i = 0; i < threads; i++) {
        args[i].mode = mode;
        args[i].tid = i;

        if (pthread_create(&tids[i], NULL, worker, &args[i]) != 0) {
            perror("pthread_create");
            return 1;
        }
    }

    sleep((unsigned int)seconds);
    atomic_store(&stop_flag, 1);

    for (int i = 0; i < threads; i++)
        pthread_join(tids[i], NULL);

    printf("spin_bench done\n");

    free(tids);
    free(args);
    return 0;
}