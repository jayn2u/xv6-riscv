#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/mman.h"

int
main(int argc, char* argv[])
{
    int ret;

    ret = mmap((void*)0x40000000, PROT_READ | PROT_WRITE);
    printf("mmap returned %d\n", ret);

    exit(0);
}