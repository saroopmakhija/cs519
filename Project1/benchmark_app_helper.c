#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/syscall.h>
#include <time.h>
#include <errno.h>

#ifndef __NR_app_helper
#define __NR_app_helper 548
#endif

#define DEFAULT_BUF_SIZE 256
#define DEFAULT_ITERS    100000
#define DEFAULT_TRIALS   5
#define WARMUP_ITERS     1000

static long long now_ns(void) {
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
        perror("clock_gettime");
        exit(1);
    }
    return (long long)ts.tv_sec * 1000000000LL + (long long)ts.tv_nsec;
}

static int verify_all_ones(const unsigned char *buf, size_t n) {
    size_t i;
    for (i = 0; i < n; i++) {
        if (buf[i] != 1) return 0;
    }
    return 1;
}

static void usage(const char *prog) {
    fprintf(stderr, "Usage: %s [buf_size_bytes] [iterations] [trials]\n", prog);
}

int main(int argc, char **argv) {
    size_t buf_size = DEFAULT_BUF_SIZE;
    long iterations = DEFAULT_ITERS;
    int trials = DEFAULT_TRIALS;

    if (argc >= 2) {
        long v = atol(argv[1]);
        if (v > 0) buf_size = (size_t)v;
        else { usage(argv[0]); return 1; }
    }
    if (argc >= 3) {
        long v = atol(argv[2]);
        if (v > 0) iterations = v;
        else { usage(argv[0]); return 1; }
    }
    if (argc >= 4) {
        long v = atol(argv[3]);
        if (v > 0) trials = (int)v;
        else { usage(argv[0]); return 1; }
    }

    unsigned char *buf = (unsigned char *)malloc(buf_size);

    printf("Buffer size: %zu bytes\n", buf_size);
    printf("Iterations:  %ld per trial\n", iterations);
    printf("Trials:      %d\n", trials);

    //correctness test
    memset(buf, 4, buf_size);
    long r = syscall(__NR_app_helper, buf, buf_size);
    if (r < 0) {
        fprintf(stderr, "syscall failed: errno=%d (%s)\n", errno, strerror(errno));
        free(buf);
        return 1;
    }
    if (!verify_all_ones(buf, buf_size)) {
        fprintf(stderr, "validation failed: buffer not all 1s after syscall\n");
        free(buf);
        return 1;
    }
    printf("Initial validation: OK\n");

    // Warmup 
    for (long i = 0; i < WARMUP_ITERS; i++) {
        memset(buf, 4, buf_size);
        r = syscall(__NR_app_helper, buf, buf_size);
        if (r < 0) {
            fprintf(stderr, "warmup syscall failed: errno=%d (%s)\n", errno, strerror(errno));
            free(buf);
            return 1;
        }
    }

    // Benchmark trials
    for (int t = 0; t < trials; t++) {
        long long start_ns, end_ns;
        // memset once
        memset(buf, 4, buf_size);

        start_ns = now_ns();
        for (long i = 0; i < iterations; i++) {
           // adds to latency so only memset once before iterations
            // memset(buf, 4, buf_size);

            r = syscall(__NR_app_helper, buf, buf_size);
            if (r < 0) {
                fprintf(stderr, "syscall failed in trial %d: errno=%d (%s)\n",
                        t + 1, errno, strerror(errno));
                free(buf);
                return 1;
            }
        }
        end_ns = now_ns();

        // Validate once at the end of the trial
        if (!verify_all_ones(buf, buf_size)) {
            fprintf(stderr, "final validation failed in trial %d\n", t + 1);
            free(buf);
            return 1;
        }

        long long total_ns = end_ns - start_ns;
        double avg_ns = (double)total_ns / (double)iterations;
        double avg_us = avg_ns / 1000.0;

        printf("Trial %d: total = %lld ns, avg = %.2f ns (%.3f us)\n",
               t + 1, total_ns, avg_ns, avg_us);
    }

    free(buf);
    return 0;
}