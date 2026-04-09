#define _GNU_SOURCE
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/syscall.h>
#include <sys/mman.h>
#include <sys/time.h>

#define DEFAULT_BUF_SIZE    1048576
#define MAX_ITERS           1000
#define MAX_PROCS           8

#ifndef sys_enable_extents
#define sys_enable_extents 335
#endif

#ifndef sys_disable_extents
#define sys_disable_extents 336
#endif

#ifndef sys_get_ex_count
#define sys_get_ex_count 337
#endif

#ifndef sys_print_pid
#define sys_print_pid 338
#endif

static long get_time_us(void) {
    struct timeval tv;
    if (gettimeofday(&tv, NULL) < 0) {
        perror("gettimeofday");
        exit(EXIT_FAILURE);
    }
    return (tv.tv_sec * 1000000L) + tv.tv_usec;
}

extern size_t buf_size;

void *create_bufs(int flags, int fd) {
    // https://stackoverflow.com/a/26259596
    char *buffer = (char *)mmap(0, buf_size, PROT_READ | PROT_WRITE,
                      flags, fd, 0);
    if (buffer == MAP_FAILED) {
        perror("mmap");
        exit(EXIT_FAILURE);
    }
    return buffer;
}

void *set_bufs(void *arg) {
    char *buffer = arg;
    for (size_t j = 0, i = 49; i < buf_size; i+=getpagesize(), j = (j+1)%getpagesize()) { // touching a random page every time (almost)
        buffer[i] = 4; // don't touch the page more than one time!!
    }
}

void p_usage(char **argv, const char *opr) {
    printf("%s <number of %s> <buffer size in bytes>\n", argv[0], opr);
    printf("example: %s 5 4096\n", argv[0]);
}

void init_extents(void) {
#ifdef USE_SYSCALL
    if (syscall(sys_enable_extents) < 0) {
        perror("syscall enable_extents (test)");
        exit(EXIT_FAILURE);
    }
#endif
}

void free_extents(int *extns, int *pages) {
    *extns = 0;
    *pages = 0;

#ifdef USE_SYSCALL
    if (syscall(sys_get_ex_count, extns, pages) < 0) {
        perror("syscall disable_extents (test)");
        exit(EXIT_FAILURE);
    }

    if (syscall(sys_disable_extents) < 0) {
        perror("syscall disable_extents (test)");
        exit(EXIT_FAILURE);
    }
#endif
}


struct output {
    int count;
    int extns;
    int pages;
    const char *opr;
    long double total_time;
};

void print_output(struct output op) {
#ifndef MINPRINT
    printf("Total time: %Lf microseconds for %d %s\n", op.total_time, op.count, op.opr);
    printf("Average time: %Lf microseconds for %d %s\n", (op.total_time/op.count), op.count, op.opr);
#ifdef USE_SYSCALL
    printf("For %d %s: %d extents and %d pages were used in total\n", op.count, op.opr, op.extns, op.pages);
    printf("For %d %s: %d extents and %d pages were used on average\n", op.count, op.opr, (op.extns/op.count), (op.pages/op.count));
#endif /* USE_SYSCALL */
#else
#ifdef USE_SYSCALL
    printf("%d,%Lf,%Lf,%d,%d,%d,%d\n",op.count,op.total_time,(op.total_time/op.count),op.extns,op.pages,(op.extns/op.count),(op.pages/op.count));
#else
    printf("%d,%Lf,%Lf\n",op.count,op.total_time,(op.total_time/op.count));
#endif /* USE_SYSCALL */
#endif /* MINPRINT */
}
