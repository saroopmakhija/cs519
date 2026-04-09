/* CS 519, Spring 2025: Project 1 - Part 2
 * IPC using pipes to perform matrix multiplication.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <errno.h>
#include <fcntl.h>

#define TILE_K 64
#define TILE_J 64

#ifndef F_SETPIPE_SZ
#define F_SETPIPE_SZ 1031
#endif

typedef struct {
    int start_row;
    int end_row;
} task_t;

static inline size_t idx(int r, int c, int n)
{
    return (size_t)r * (size_t)n + (size_t)c;
}

static void die(const char *msg)
{
    perror(msg);
    exit(EXIT_FAILURE);
}

// write all bytes, handling partial writes
static ssize_t write_all(int fd, const void *buf, size_t len)
{
    const char *p = (const char *)buf;
    size_t done = 0;
    while (done < len) {
        ssize_t rc = write(fd, p + done, len - done);
        if (rc < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        done += (size_t)rc;
    }
    return (ssize_t)done;
}

// read all bytes, handling partial reads
static ssize_t read_all(int fd, void *buf, size_t len)
{
    char *p = (char *)buf;
    size_t done = 0;
    while (done < len) {
        ssize_t rc = read(fd, p + done, len - done);
        if (rc < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        if (rc == 0) break;
        done += (size_t)rc;
    }
    return (ssize_t)done;
}

static double elapsed_sec(struct timeval *t0, struct timeval *t1)
{
    return (t1->tv_sec - t0->tv_sec) +
           (t1->tv_usec - t0->tv_usec) * 1e-6;
}

void print_stats(int n, int nprocs, int ok, double t)
{
    printf("Input size: %d x %d\n",        n, n);
    printf("Number of processes: %d\n",     nprocs);
    printf("Verification: %s\n",            ok ? "PASS" : "FAIL");
    printf("Total runtime: %.6f seconds\n", t);
}

static int *alloc_int_mat(int n)
{
    int *m = malloc((size_t)n * n * sizeof(int));
    if (!m) die("malloc int");
    return m;
}

static long long *alloc_ll_mat(int n)
{
    long long *m = malloc((size_t)n * n * sizeof(long long));
    if (!m) die("malloc ll");
    return m;
}

// B stored transposed so the inner dot product is stride-1
static void init_matrices(int *restrict A, int *restrict BT, int n)
{
    for (int i = 0; i < n; i++)
        for (int j = 0; j < n; j++) {
            A[idx(i, j, n)]  = i + j;
            BT[idx(j, i, n)] = i + j;
        }
}

// closed-form expected value for verification
static long long expected(int row, int col, int n)
{
    long long nn = n;
    long long s1 = nn * (nn - 1) / 2;
    long long s2 = nn * (nn - 1) * (2 * nn - 1) / 6;
    return s2 + ((long long)row + col) * s1 + nn * (long long)row * col;
}

static int verify(const long long *C, int n)
{
    for (int i = 0; i < n; i++)
        for (int j = 0; j < n; j++)
            if (C[idx(i, j, n)] != expected(i, j, n))
                return 0;
    return 1;
}

// figure out which rows belong to a given rank
static void row_range(int rank, int P, int n, int *r0, int *r1)
{
    int base  = n / P;
    int extra = n % P;
    *r0 = rank * base + (rank < extra ? rank : extra);
    *r1 = *r0 + base  + (rank < extra ? 1 : 0);
}

// tiled multiply, loop order kk->jj->i keeps BT sub-block in cache
static void multiply_rows(int n, int r0, int r1,
                           const int *restrict A,
                           const int *restrict BT,
                           long long  *restrict C)
{
    int nrows = r1 - r0;
    memset(C, 0, (size_t)nrows * (size_t)n * sizeof(long long));

    for (int kk = 0; kk < n; kk += TILE_K) {
        int k_end = kk + TILE_K;
        if (k_end > n) k_end = n;

        for (int jj = 0; jj < n; jj += TILE_J) {
            int j_end = jj + TILE_J;
            if (j_end > n) j_end = n;

            for (int i = 0; i < nrows; i++) {
                int gi = r0 + i;
                const int *restrict a_row = &A[idx(gi, 0, n)];
                long long  *restrict c_row = &C[(size_t)i * n];

                for (int j = jj; j < j_end; j++) {
                    const int *restrict bt_row = &BT[idx(j, 0, n)];
                    long long sum = 0;

                    for (int k = kk; k < k_end; k++) {
                        sum += (long long)a_row[k] * (long long)bt_row[k];
                    }
                    c_row[j] += sum;
                }
            }
        }
    }
}

// child reads its task from the pipe, computes, and writes result back
static void child_work(int n, int task_fd, int write_fd,
                        const int *A, const int *BT)
{
    task_t task;
    if (read_all(task_fd, &task, sizeof(task)) != (ssize_t)sizeof(task))
        _exit(1);
    close(task_fd);

    int r0 = task.start_row;
    int r1 = task.end_row;
    int nrows = r1 - r0;

    long long *buf = malloc((size_t)nrows * n * sizeof(long long));
    if (!buf) _exit(1);

    multiply_rows(n, r0, r1, A, BT, buf);

    size_t bytes = (size_t)nrows * n * sizeof(long long);
    if (write_all(write_fd, buf, bytes) != (ssize_t)bytes)
        _exit(1);

    free(buf);
    close(write_fd);
    _exit(0);
}

int main(int argc, char const *argv[])
{
    int n = 1000, P = 4;
    if (argc >= 2) n = atoi(argv[1]);
    if (argc >= 3) P = atoi(argv[2]);
    if (n <= 0 || P <= 0) {
        fprintf(stderr, "Usage: %s <matrix_size> <num_processes>\n", argv[0]);
        return 1;
    }
    if (P > n) P = n;

    int *A  = alloc_int_mat(n);
    int *BT = alloc_int_mat(n);
    long long *C = alloc_ll_mat(n);
    init_matrices(A, BT, n);

    // pre-compute row ranges
    int *r0s = malloc((size_t)P * sizeof(int));
    int *r1s = malloc((size_t)P * sizeof(int));
    if (!r0s || !r1s) die("malloc ranges");
    for (int i = 0; i < P; i++)
        row_range(i, P, n, &r0s[i], &r1s[i]);

    // set up task pipes (parent->child) and result pipes (child->parent)
    int (*task_pipes)[2] = malloc((size_t)P * sizeof(*task_pipes));
    int (*res_pipes)[2]  = malloc((size_t)P * sizeof(*res_pipes));
    pid_t *pids          = malloc((size_t)P * sizeof(pid_t));
    if (!task_pipes || !res_pipes || !pids) die("malloc");

    for (int i = 0; i < P; i++) {
        if (pipe(task_pipes[i]) == -1) die("pipe task");
        if (pipe(res_pipes[i]) == -1) die("pipe result");
        fcntl(res_pipes[i][1], F_SETPIPE_SZ, 1 << 20);
    }

    struct timeval t0, t1;
    gettimeofday(&t0, NULL);

    // fork children
    for (int i = 0; i < P; i++) {
        pid_t pid = fork();
        if (pid < 0) die("fork");

        if (pid == 0) {
            for (int j = 0; j < P; j++) {
                close(task_pipes[j][1]);
                close(res_pipes[j][0]);
                if (j != i) {
                    close(task_pipes[j][0]);
                    close(res_pipes[j][1]);
                }
            }
            child_work(n, task_pipes[i][0], res_pipes[i][1], A, BT);
        }

        pids[i] = pid;
        close(task_pipes[i][0]);
        close(res_pipes[i][1]);
    }

    // send task assignments to each child
    for (int i = 0; i < P; i++) {
        task_t task;
        task.start_row = r0s[i];
        task.end_row   = r1s[i];
        if (write_all(task_pipes[i][1], &task, sizeof(task)) != (ssize_t)sizeof(task))
            die("write task");
        close(task_pipes[i][1]);
    }

    // collect results from each child directly into C
    for (int i = 0; i < P; i++) {
        int nrows = r1s[i] - r0s[i];
        size_t bytes = (size_t)nrows * n * sizeof(long long);
        if (read_all(res_pipes[i][0], &C[idx(r0s[i], 0, n)], bytes) != (ssize_t)bytes)
            die("read result");
        close(res_pipes[i][0]);
    }

    gettimeofday(&t1, NULL);
    double elapsed = elapsed_sec(&t0, &t1);

    // wait for children and check they exited ok
    int children_ok = 1;
    for (int i = 0; i < P; i++) {
        int st;
        if (waitpid(pids[i], &st, 0) == -1) die("waitpid");
        if (!WIFEXITED(st) || WEXITSTATUS(st) != 0) children_ok = 0;
    }

    int ok = children_ok && verify(C, n);
    print_stats(n, P, ok, elapsed);

    free(A); free(BT); free(C);
    free(task_pipes); free(res_pipes); free(pids);
    free(r0s); free(r1s);
    return 0;
}
