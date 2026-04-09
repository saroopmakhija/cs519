#include <stdlib.h>
#include <test.h>

size_t buf_size = DEFAULT_BUF_SIZE;

void perform_iter(int *extns_cnt, int *pages_cnt, struct output *op) {
    long start_time, end_time;
#ifndef IS_FILE
    void *buffer = create_bufs(MAP_PRIVATE | MAP_ANONYMOUS, 0);
#else
    const char *filepath = "/tmp/mmapped.bin";
    int fd, result;
    fd = open(filepath, O_RDWR | O_CREAT | O_TRUNC, (mode_t)0777);
    if (fd == -1) {
    	perror("Error opening file for writing");
    	exit(EXIT_FAILURE);
    }

    result = lseek(fd, buf_size-1, SEEK_SET);
    if (result == -1) {
    	close(fd);
    	perror("Error calling lseek() to 'stretch' the file");
    	exit(EXIT_FAILURE);
    }

    result = write(fd, "", 1);
    if (result != 1) {
    	close(fd);
    	perror("Error writing last byte of the file");
    	exit(EXIT_FAILURE);
    }
    void *buffer = create_bufs(MAP_SHARED, fd);
#endif
    start_time = get_time_us();
    init_extents();
    set_bufs(buffer);
    free_extents(extns_cnt, pages_cnt);
    end_time = get_time_us();
    op->extns += *extns_cnt;
    op->pages += *pages_cnt;
    op->total_time += (end_time - start_time);
    if (!buffer) {
        fprintf(stderr, "Error in buffer...");
        exit(EXIT_FAILURE);
    }

    memset(buffer, 0, buf_size);
    if (munmap(buffer, buf_size) != 0) {
        fprintf(stderr, "Error in buffer...");
        exit(EXIT_FAILURE);
    }
#ifdef IS_FILE
    close(fd);
    remove(filepath);
#endif
}


void perform_oprs(int iter) {
    int *extns_cnt, *pages_cnt, i;
    struct output op = {
        .extns = 0,
        .pages = 0,
        .count = iter,
        .opr = "iterations",
        .total_time = 0
    };
    extns_cnt = (int*)malloc(sizeof(int));
    pages_cnt = (int*)malloc(sizeof(int));
    for (i = 0; i < iter; i++) {
        perform_iter(extns_cnt, pages_cnt, &op);
    }
    print_output(op);
    free(extns_cnt);
    free(pages_cnt);
}

int main(int argc, char **argv) {
    int iter;

    if (argc == 1) {
        p_usage(argv, "operations");
        exit(EXIT_FAILURE);
    } else if (argc > 1) {
        iter = atoi(argv[1]);
        if (iter == 0) {
            fprintf(stderr, "Invalid operations count provided.\n");
            exit(EXIT_FAILURE);
        }

        if (argc > 2) {
            buf_size = (size_t)atoi(argv[2]);
            if (buf_size == 0) {
                fprintf(stderr, "Invalid buffer size provided. Using default %d bytes.\n", DEFAULT_BUF_SIZE);
            }
        }
    }
#ifndef MINPRINT
    printf("Using buffer size: %zu bytes and performing %d operations.\n", buf_size, iter);
#endif
    perform_oprs(iter);
    return EXIT_SUCCESS;
}
