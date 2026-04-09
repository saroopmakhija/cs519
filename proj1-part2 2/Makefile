CC = gcc
CFLAGS = -O3 -march=native -funroll-loops -Wall -Wextra -std=c11
LDFLAGS =

TARGETS = IPC-pipe IPC-shmem

all: $(TARGETS)

IPC-pipe: IPC-pipe.c
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

IPC-shmem: IPC-shmem.c
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

clean:
	rm -f $(TARGETS) *.o *.data *.txt *.csv

.PHONY: all clean