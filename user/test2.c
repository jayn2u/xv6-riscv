#include "kernel/types.h"
#include "user/user.h"

static int
test_completion_order(void)
{
  int gate[2], result[2];
  int pids[3], count = 0;
  int values[3] = {19, 34, 6};
  char names[3] = {'C', 'B', 'A'};
  char order[4] = {0};

  if(pipe(gate) < 0 || pipe(result) < 0)
    return 1;

  for(int i = 0; i < 3; i++){
    int pid = fork();
    if(pid < 0)
      goto fail;

    if(pid == 0){
      char token;
      close(gate[1]);
      close(result[0]);

      if(read(gate[0], &token, 1) != 1)
        exit(1);
      close(gate[0]);

      for(volatile int n = 0; n < 10000000; n++)
        ;

      if(write(result[1], &names[i], 1) != 1)
        exit(1);
      close(result[1]);
      exit(0);
    }

    pids[count++] = pid;
    if(setnice(pid, values[i]) < 0)
      goto fail;
  }

  close(gate[0]);
  close(result[1]);

  if(write(gate[1], "xxx", 3) != 3)
    goto fail;
  close(gate[1]);

  for(int i = 0; i < 3; i++)
    if(read(result[0], &order[i], 1) != 1)
      goto fail;
  close(result[0]);

  for(int i = 0; i < 3; i++)
    wait(0);

  printf("completion order: %s\n", order);
  return strcmp(order, "ACB") != 0;

fail:
  for(int i = 0; i < count; i++)
    kill(pids[i]);
  while(wait(0) >= 0)
    ;
  return 1;
}

static int
test_same_nice_interleave(void)
{
  int gate[2] = {-1, -1}, result[2] = {-1, -1};
  int pids[2], count = 0;
  int counts[2] = {0, 0}, changes = 0;
  char order[17] = {0};

  if(pipe(gate) < 0 || pipe(result) < 0)
    goto fail;

  for(int i = 0; i < 2; i++){
    int pid = fork();
    if(pid < 0)
      goto fail;
    if(pid == 0){
      char token, letter = 'A' + i;
      close(gate[1]);
      close(result[0]);
      if(read(gate[0], &token, 1) != 1)
        exit(1);
      close(gate[0]);

      for(int j = 0; j < 8; j++){
        for(volatile int n = 0; n < 1000000000; n++)
          ;
        if(write(result[1], &letter, 1) != 1)
          exit(1);
      }
      close(result[1]);
      exit(0);
    }
    pids[count++] = pid;
    if(setnice(pid, 10) < 0)
      goto fail;
  }

  close(gate[0]);
  gate[0] = -1;
  close(result[1]);
  result[1] = -1;
  if(write(gate[1], "xx", 2) != 2)
    goto fail;
  close(gate[1]);
  gate[1] = -1;

  for(int i = 0; i < 16; i++){
    if(read(result[0], &order[i], 1) != 1)
      goto fail;
    if(order[i] != 'A' && order[i] != 'B')
      goto fail;
    counts[order[i] - 'A']++;
    if(i > 0 && order[i] != order[i - 1])
      changes++;
  }
  close(result[0]);
  result[0] = -1;

  for(int i = 0; i < count; i++){
    int status;
    if(wait(&status) < 0)
      goto fail;
    if(status != 0)
      goto fail;
  }

  printf("same nice order: %s (changes: %d)\n", order, changes);
  return counts[0] != 8 || counts[1] != 8 || changes < 2;

fail:
  for(int i = 0; i < count; i++)
    kill(pids[i]);
  for(int i = 0; i < 2; i++){
    if(gate[i] >= 0)
      close(gate[i]);
    if(result[i] >= 0)
      close(result[i]);
  }
  while(wait(0) >= 0)
    ;
  return 1;
}

static int
test_lower_nice_switches_immediately(void)
{
  int result[2];
  char order[4] = {0};
  int pid, bad = 0;

  if(pipe(result) < 0)
    return 1;
  if(setnice(getpid(), 10) < 0)
    return 1;

  pid = fork();
  if(pid < 0)
    return 1;
  if(pid == 0){
    close(result[0]);
    if(write(result[1], "C", 1) != 1)
      exit(1);
    exit(0);
  }

  if(setnice(pid, 30) < 0 ||
     write(result[1], "P", 1) != 1 ||
     setnice(pid, 0) < 0 ||
     write(result[1], "Q", 1) != 1)
    bad = 1;

  close(result[1]);
  if(read(result[0], order, 3) != 3)
    bad = 1;
  close(result[0]);
  if(wait(0) < 0)
    bad = 1;
  setnice(getpid(), 0);

  printf("lower nice order: %s\n", order);
  return bad || strcmp(order, "PCQ") != 0;
}

int
main(void)
{
  int failed = 0;

  if(setnice(getpid(), 0) < 0)
    exit(1);

  failed += test_completion_order();
  failed += test_same_nice_interleave();
  failed += test_lower_nice_switches_immediately();

  if(failed){
    fprintf(2, "scheduler test failed (%d)\n", failed);
    exit(1);
  }
  printf("scheduler test passed\n");
  exit(0);
}
