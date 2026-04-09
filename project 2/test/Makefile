N_COMP := -O0 -g -I.
SYS_COMP := $(N_COMP) -DUSE_SYSCALL

ifdef MIN_PRINT
N_COMP := $(N_COMP) -DMINPRINT
SYS_COMP := $(SYS_COMP) -DMINPRINT
endif

ifdef IS_FILE
N_COMP := $(N_COMP) -DIS_FILE
SYS_COMP := $(SYS_COMP) -DIS_FILE
endif

CC = gcc
OUTPUT=ast.o east.o st.o est.o



all: $(OUTPUT)

st.o: test_sync.c test.h
	$(CC) test_sync.c $(N_COMP) -o $@

est.o: test_sync.c test.h
	$(CC) test_sync.c $(SYS_COMP) -o $@

ast.o: test_async.c test.h
	$(CC) test_async.c $(N_COMP) -o $@ -lpthread

east.o: test_async.c test.h
	$(CC) test_async.c $(SYS_COMP) -o $@ -lpthread

clean:
	rm -f $(OUTPUT)
