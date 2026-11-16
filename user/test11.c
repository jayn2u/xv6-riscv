#include "kernel/types.h"
#include "user/user.h"

static int
test_failed_setnice(void)
{
  int gate[2] = {-1, -1}, result[2] = {-1, -1};
  int me = getpid(), pid = -1, status, bad = 0;
  int rejected, unchanged, changed;
  char order[4] = {0}, extra;

  if(pipe(gate) < 0 || pipe(result) < 0)
    goto fail;
  if(setnice(me, 10) != 0 || getnice(me) != 10)
    goto fail;

  pid = fork();
  if(pid < 0)
    goto fail;
  if(pid == 0){
    char token;
    close(gate[1]);
    close(result[0]);
    if(read(gate[0], &token, 1) != 1 || token != 'x')
      exit(1);
    close(gate[0]);
    if(getnice(getpid()) != 5 || write(result[1], "C", 1) != 1)
      exit(1);
    close(result[1]);
    exit(0);
  }

  close(gate[0]);
  gate[0] = -1;
  // Success yields to the higher-priority child, which blocks on the gate.
  if(setnice(pid, 5) != 0 || getnice(pid) != 5)
    goto fail;

  // Release before the invalid call. A timer may run C at any point now.
  if(write(gate[1], "x", 1) != 1)
    goto fail;
  rejected = setnice(me, 45);
  if(write(result[1], "P", 1) != 1)
    goto fail;
  unchanged = getnice(me);
  changed = setnice(me, 20);
  if(write(result[1], "Q", 1) != 1)
    goto fail;
  if(rejected != -1 || unchanged != 10 || changed != 0 ||
     getnice(me) != 20)
    bad = 1;

  close(gate[1]);
  gate[1] = -1;
  close(result[1]);
  result[1] = -1;
  // Pipe reads may be short. Read each marker and require EOF afterwards.
  for(int i = 0; i < 3; i++)
    if(read(result[0], &order[i], 1) != 1)
      goto fail;
  if(read(result[0], &extra, 1) != 0)
    goto fail;
  close(result[0]);
  result[0] = -1;

  if(wait(&status) != pid)
    goto fail;
  pid = -1;
  if(status != 0)
    bad = 1;

  printf("setnice(45): %d, nice after failure: %d\n", rejected, unchanged);
  printf("marker order: %s (PCQ or CPQ allowed on one CPU)\n", order);
  // C must precede Q; C versus P is diagnostic.
  if(strcmp(order, "PCQ") != 0 && strcmp(order, "CPQ") != 0)
    bad = 1;
  return bad;

fail:
  if(pid > 0)
    kill(pid);
  for(int i = 0; i < 2; i++){
    if(gate[i] >= 0)
      close(gate[i]);
    if(result[i] >= 0)
      close(result[i]);
  }
  if(pid > 0)
    wait(&status);
  return 1;
}

int
main(void)
{
  printf("test11: run with CPUS=1\n");
  if(test_failed_setnice()){
    fprintf(2, "test11 failed\n");
    exit(1);
  }
  printf("test11 observable checks passed\n");
  printf("No voluntary yield on failure: unproven without instrumentation.\n");
  // Even PCQ does not identify which kernel paths ran.
  // CPQ can occur on a timer tick, including before the invalid call.
  // These markers are events, not a scheduler trace.
  exit(0);
}
