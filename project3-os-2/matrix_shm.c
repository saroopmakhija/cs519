/* CS 519, Spring 2025: Project 1 - Part 2
* IPC using shared memory to perform matrix multiplication.
*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/shm.h>
#include <sys/ipc.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <errno.h>

// tile sizes for the blocked multiply
#define TILE_K 64
#define TILE_J 64

static inline size_t idx(int r, int c, int n)
{
    return (size_t)r * (size_t)n + (size_t)c;
}

static void die(const char *msg)
{
    perror(msg);
    exit(EXIT_FAILURE);
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

// shared memory helpers
static int shm_create(size_t bytes)
{
    int id = shmget(IPC_PRIVATE, bytes, IPC_CREAT | 0600);
    if (id == -1) die("shmget");
    return id;
}

static void *shm_attach(int id)
{
    void *p = shmat(id, NULL, 0);
    if (p == (void *)-1) die("shmat");
    return p;
}

static void shm_detach(void *p)  { if (shmdt(p) == -1) die("shmdt"); }
static void shm_remove(int id)   { shmctl(id, IPC_RMID, NULL); }

// B stored transposed so the inner dot product is stride-1
static void init_matrices(int *restrict A, int *restrict BT, int n)
{
    for (int i = 0; i < n; i++) {
        for (int j = 0; j < n; j++) {
            A[idx(i, j, n)]  = i + j;
            BT[idx(j, i, n)] = i + j;
        }
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

// tiled multiply, loop order kk->jj->i keeps BT sub-block in cache
static void multiply_rows(int n, int r0, int r1,
                           const int *restrict A,
                           const int *restrict BT,
                           long long  *restrict C)
{
    memset(&C[idx(r0, 0, n)], 0,
           (size_t)(r1 - r0) * (size_t)n * sizeof(long long));

    for (int kk = 0; kk < n; kk += TILE_K) {
        int k_end = kk + TILE_K;
        if (k_end > n) k_end = n;

        for (int jj = 0; jj < n; jj += TILE_J) {
            int j_end = jj + TILE_J;
            if (j_end > n) j_end = n;

            for (int i = r0; i < r1; i++) {
                const int *restrict a_row = &A[idx(i, 0, n)];
                long long  *restrict c_row = &C[idx(i, 0, n)];

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

// figure out which rows belong to a given rank
static void row_range(int rank, int P, int n, int *r0, int *r1)
{
    int base  = n / P;
    int extra = n % P;
    *r0 = rank * base + (rank < extra ? rank : extra);
    *r1 = *r0 + base  + (rank < extra ? 1 : 0);
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

    // allocate matrices in shared memory
    size_t elems = (size_t)n * n;
    int shm_a  = shm_create(elems * sizeof(int));
    int shm_bt = shm_create(elems * sizeof(int));
    int shm_c  = shm_create(elems * sizeof(long long));

    int      *A  = (int *)shm_attach(shm_a);
    int      *BT = (int *)shm_attach(shm_bt);
    long long *C = (long long *)shm_attach(shm_c);

    init_matrices(A, BT, n);

    pid_t *pids = NULL;
    if (P > 1) {
        pids = malloc((size_t)(P - 1) * sizeof(pid_t));
        if (!pids) die("malloc pids");
    }

    struct timeval t0, t1;
    gettimeofday(&t0, NULL);

    // fork children, each one works on its own row range
    for (int rank = 1; rank < P; rank++) {
        pid_t pid = fork();
        if (pid < 0) die("fork");

        if (pid == 0) {
            int r0, r1;
            row_range(rank, P, n, &r0, &r1);
            multiply_rows(n, r0, r1, A, BT, C);
            shm_detach(A); shm_detach(BT); shm_detach(C);
            _exit(0);
        }
        pids[rank - 1] = pid;
    }

    // parent also does its share
    {
        int r0, r1;
        row_range(0, P, n, &r0, &r1);
        multiply_rows(n, r0, r1, A, BT, C);
    }

    // wait for children
    int children_ok = 1;
    for (int i = 0; i < P - 1; i++) {
        int st;
        if (waitpid(pids[i], &st, 0) == -1) die("waitpid");
        if (!WIFEXITED(st) || WEXITSTATUS(st) != 0) children_ok = 0;
    }

    gettimeofday(&t1, NULL);
    double elapsed = elapsed_sec(&t0, &t1);
    int ok = children_ok && verify(C, n);

    print_stats(n, P, ok, elapsed);

    free(pids);
    shm_detach(A);  shm_detach(BT);  shm_detach(C);
    shm_remove(shm_a); shm_remove(shm_bt); shm_remove(shm_c);
    return 0;
}