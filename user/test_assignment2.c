#include "kernel/types.h"
#include "user/user.h"

int
main(void)
{
  int gate[2], result[2];
  int pids[3], count = 0;
  int values[3] = {28, 12, 4};
  char names[3] = {'C', 'B', 'A'};
  char order[4] = {0};

  if(setnice(getpid(), 0) < 0)
    exit(1);
  if(pipe(gate) < 0 || pipe(result) < 0)
    exit(1);

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
  exit(strcmp(order, "ABC") != 0);

fail:
  for(int i = 0; i < count; i++)
    kill(pids[i]);
  while(wait(0) >= 0)
    ;
  fprintf(2, "schedtest failed\n");
  exit(1);
}
