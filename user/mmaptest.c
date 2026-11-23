#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/mman.h"

static volatile char cowbuf[4096] __attribute__((aligned(4096)));

static void test_mmap(void)
{
    int ret;

    ret = mmap((void *)0x40000000, PROT_READ | PROT_WRITE);
    printf("mmap returned %d\n", ret); // expect 0

    ret = munmap((void *)0x50000000);
    printf("munmap wrong addr: %d\n", ret); // expect -1

    ret = munmap((void *)0x40000000);
    printf("munmap returned %d\n", ret); // expect 0

    ret = munmap((void *)0x40000000);
    printf("munmap again: %d\n", ret); // expect -1

}

static void test_cow(void)
{
    cowbuf[0] = 'A';

    printf("Before fork: buffer = %c\n", cowbuf[0]);
    printf("Find the mapping for cowbuf VA = %p in each snapshot\n",
           (void *)cowbuf);
    int pid = fork();
    if (pid < 0)
    {
        printf("fork failed\n");
        exit(1);
    }

    if (pid == 0)
    {
        printf("=== Child BEFORE writing cowbuf ===\n");
        printpagetbl();
        cowbuf[0] = 'B';
        printf("=== Child AFTER writing cowbuf ===\n");
        printpagetbl();
        printf("Child after write: buffer = %c (expected B)\n", cowbuf[0]);
        if (cowbuf[0] != 'B')
            exit(1);

        exit(0);
    }

    int status = -1;
    int child = wait(&status);
    printf("=== Parent AFTER child finishes ===\n");
    printpagetbl();
    printf("Parent after waiting: buffer = %c (expected A)\n", cowbuf[0]);

    if (child != pid || status != 0)
    {
        printf("COW test FAILED: child error\n");
        exit(1);
    }

    if (cowbuf[0] != 'A')
    {
        printf("COW test FAILED: parent memory changed\n");
        exit(1);
    }

    printf("COW isolation test PASSED\n");
}

int main(int argc, char *argv[])
{
    if (argc != 2)
    {
        fprintf(2, "Usage: mmaptest 1|2|3\n1: mmap/munmap\n2: printpagetbl\n3: COW\n");
        exit(1);
    }

    if (strcmp(argv[1], "1") == 0)
        test_mmap();
    else if (strcmp(argv[1], "2") == 0)
        printpagetbl();
    else if (strcmp(argv[1], "3") == 0)
        test_cow();
    else
    {
        fprintf(2, "Unknown test: %s. Use 1, 2, or 3.\n", argv[1]);
        exit(1);
    }

    exit(0);
}
