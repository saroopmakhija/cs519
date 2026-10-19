#include <stdio.h>
#include <unistd.h>
#include <sys/syscall.h>
#include <errno.h>
#include <string.h>

#define SYS_coop_sched 449

int main(void)
{
    long ret;

    ret = syscall(SYS_coop_sched, 1);
    if (ret < 0) {
        printf("coop_sched(1) failed: %s\n", strerror(errno));
        return 1;
    }
    printf("coop_sched(1) worked\n");

    ret = syscall(SYS_coop_sched, 0);
    if (ret < 0) {
        printf("coop_sched(0) failed: %s\n", strerror(errno));
        return 1;
    }
    printf("coop_sched(0) worked\n");

    ret = syscall(SYS_coop_sched, 2);
    if (ret < 0)
        printf("coop_sched(2) correctly failed: %s\n", strerror(errno));
    else
        printf("ERROR: coop_sched(2) should have failed\n");

    return 0;
}