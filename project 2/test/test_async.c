#include <pthread.h>
#include <stdlib.h>
#include <test.h>

size_t buf_size = DEFAULT_BUF_SIZE;

void create_join_threads(pthread_t *ptid, void **buffers, struct output *op) {
    int *extns_cnt, *pages_cnt, i;
    long start_time, end_time;
#ifdef IS_FILE
    #define FILEPATH "/tmp/mmapped.bin"
    int *fd, result, len;
    char **filepath;
    char str[32];
    fd = calloc(sizeof(int), op->count);
    filepath = calloc(sizeof(char*), op->count);
#endif
    extns_cnt = (int*)calloc(sizeof(int), 1);
    pages_cnt = (int*)calloc(sizeof(int), 1);
    for (i = 0; i < op->count; i++) {
#ifndef IS_FILE
        buffers[i] = create_bufs(MAP_PRIVATE | MAP_ANONYMOUS, 0);
#else
    memset(str, 0, 32);
    sprintf(str, "%d", i);
    len = sizeof(FILEPATH) + strlen(str);
    filepath[i] = malloc(len);
    strcpy(filepath[i], FILEPATH);
    strncpy(filepath[i]+(sizeof(FILEPATH)-1), str, strlen(str));
    filepath[i][len-1] = '\0';
    fd[i] = open(filepath[i], O_RDWR | O_CREAT | O_TRUNC, (mode_t)0777);
    if (fd[i] == -1) {
    	perror("Error opening file for writing");
    	exit(EXIT_FAILURE);
    }

    result = lseek(fd[i], buf_size-1, SEEK_SET);
    if (result == -1) {
       	close(fd[i]);
    	perror("Error calling lseek() to 'stretch' the file");
    	exit(EXIT_FAILURE);
    }

    result = write(fd[i], "", 1);
    if (result != 1) {
    	close(fd[i]);
    	perror("Error writing last byte of the file");
    	exit(EXIT_FAILURE);
    }
    buffers[i] = create_bufs(MAP_SHARED, fd[i]);
#endif
    }
    start_time = get_time_us();
    init_extents();
    for (i = 0; i < op->count; i++) {
        pthread_create(&ptid[i], NULL, &set_bufs, buffers[i]);
    }
    for (i = 0; i < op->count; i++) {
        pthread_join(ptid[i], NULL);
    }
    free_extents(extns_cnt, pages_cnt);
    end_time = get_time_us();
    op->extns = *extns_cnt;
    op->pages = *pages_cnt;
    op->total_time = (end_time - start_time);
    free(extns_cnt);
    free(pages_cnt);
    for (i = 0; i < op->count; i++) {
        if (!buffers[i]) {
            fprintf(stderr, "Error in buffer...");
            exit(EXIT_FAILURE);
        }

        memset(buffers[i], 0, buf_size);
        if (munmap(buffers[i], buf_size) != 0) {
            fprintf(stderr, "Error in buffer...");
            exit(EXIT_FAILURE);
        }
#ifdef IS_FILE
        close(fd[i]);
        remove(filepath[i]);
        free(filepath[i]);
#endif
    }
#ifdef IS_FILE
    free(fd);
    free(filepath);
#endif
}

void perform_oprs(int threads) {
    long start_time, end_time;
    pthread_t *ptid = calloc(sizeof(pthread_t), threads);
    void **buffers = calloc(sizeof(void*), threads);
    struct output op = {
        .extns = 0,
        .pages = 0,
        .count = threads,
        .opr = "iterations",
        .total_time = 0
    };
    create_join_threads(ptid, buffers, &op);
    print_output(op);
    free(buffers);
    free(ptid);
}

int main(int argc, char **argv) {
    int threads;

    if (argc == 1) {
        p_usage(argv, "threads");
        exit(EXIT_FAILURE);
    } else if (argc > 1) {
        threads = atoi(argv[1]);
        if (threads == 0) {
            fprintf(stderr, "Invalid thread count provided. Using default %d threads.\n", MAX_PROCS);
            threads = MAX_PROCS;
        }

        if (argc > 2) {
            buf_size = (size_t)atoi(argv[2]);
            if (buf_size == 0) {
                fprintf(stderr, "Invalid buffer size provided. Using default %d bytes.\n", DEFAULT_BUF_SIZE);
            }
        }
    }
#ifndef MINPRINT
    printf("Using buffer size: %zu bytes and running on %d threads.\n", buf_size, threads);
#endif
    perform_oprs(threads);
    return EXIT_SUCCESS;
}
