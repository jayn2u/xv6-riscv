#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/mman.h"

int
main(int argc, char* argv[])
{
    int ret;

    ret = mmap((void*)0x40000000, PROT_READ | PROT_WRITE);
    printf("mmap returned %d\n", ret);          // expect 0

    ret = munmap((void*)0x50000000);
    printf("munmap wrong addr: %d\n", ret);     // expect -1

    ret = munmap((void*)0x40000000);
    printf("munmap returned %d\n", ret);        // expect 0

    ret = munmap((void*)0x40000000);
    printf("munmap again: %d\n", ret);          // expect -1

    exit(0);
}