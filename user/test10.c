#include "kernel/types.h"
#include "user/user.h"

static int
test_zombie_nice(void)
{
  int pid, nice, status, bad = 0;

  pid = fork();
  if(pid < 0){
    fprintf(2, "test10: fork failed\n");
    return 1;
  }
  if(pid == 0){
    if(setnice(getpid(), 8) < 0)
      exit(1);
    exit(0);
  }

  // kwait copies status only after finding a ZOMBIE. This invalid address
  // makes copyout fail before freeproc, leaving our only child unreaped.
  // A pipe byte or EOF would arrive before exit has finished.
  if(wait((int *)-1) != -1){
    fprintf(2, "test10: invalid-status wait unexpectedly succeeded\n");
    return 1;
  }

  nice = getnice(pid);
  printf("zombie child nice: %d (expected 8)\n", nice);
  ps(pid);
  if(nice != 8)
    bad = 1;

  if(wait(&status) != pid){
    fprintf(2, "test10: reap failed\n");
    kill(pid);
    while(wait(0) >= 0)
      ;
    return 1;
  }
  if(status != 0){
    fprintf(2, "test10: child status %d (expected 0)\n", status);
    bad = 1;
  }
  return bad;
}

int
main(void)
{
  if(test_zombie_nice()){
    fprintf(2, "test10 failed\n");
    exit(1);
  }
  printf("test10 passed\n");
  exit(0);
}
