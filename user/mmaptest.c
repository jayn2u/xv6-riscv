#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/mman.h"

static volatile char cowbuf[4096] __attribute__((aligned(4096)));

int
main(int argc, char* argv[])
{
    int ret;

    cowbuf[0] = 'A';

    ret = mmap((void*)0x40000000, PROT_READ | PROT_WRITE);
    printf("mmap returned %d\n", ret);          // expect 0

    ret = munmap((void*)0x50000000);
    printf("munmap wrong addr: %d\n", ret);     // expect -1

    ret = munmap((void*)0x40000000);
    printf("munmap returned %d\n", ret);        // expect 0

    ret = munmap((void*)0x40000000);
    printf("munmap again: %d\n", ret);          // expect -1

    printpagetbl();

    int pid = fork();
    if (pid < 0) {
        printf("fork failed\n");
        exit(1);
    }

    if (pid == 0) {
        cowbuf[0] = 'B';
        if (cowbuf[0] != 'B')
            exit(1);

        exit(0);
    }

    int status = -1;
    int child = wait(&status);

    if(child != pid || status != 0) {
        printf("COW test FAILED: child error\n");
        exit(1);
    }

    if (cowbuf[0] != 'A') {
        printf("COW test FAILED: parent memory changed\n");
        exit(1);
    }

    printf("COW isolation test PASSED\n");
    exit(0);
}
