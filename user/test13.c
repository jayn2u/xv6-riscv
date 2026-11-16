#include "kernel/types.h"
#include "user/user.h"

static int
test_priority_groups(void)
{
  int gate[2] = {-1, -1}, result[2] = {-1, -1};
  int pids[4] = {0}, count = 0;
  int values[4] = {5, 5, 15, 15};
  int counts[4] = {0}, changes[2] = {0};
  int bad = 0;
  char order[21] = {0}, extra;

  // Stay above the workers during setup, then sleep in wait().
  if(setnice(getpid(), 0) < 0 ||
     pipe(gate) < 0 || pipe(result) < 0)
    goto fail;

  for(int i = 0; i < 4; i++){
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
      if(getnice(getpid()) != values[i])
        exit(1);

      // Busy polling keeps the worker runnable. No pause or console I/O.
      // A marker samples progress after a tick change, not each dispatch.
      for(int j = 0; j < 5; j++){
        uint start = uptime();
        while((uint)uptime() - start < 1)
          ;
        if(write(result[1], &letter, 1) != 1)
          exit(1);
      }
      close(result[1]);
      exit(0);
    }
    pids[count++] = pid;
    if(setnice(pid, values[i]) < 0)
      goto fail;
  }

  close(gate[0]);
  gate[0] = -1;
  close(result[1]);
  result[1] = -1;
  if(write(gate[1], "xxxx", 4) != 4)
    goto fail;
  close(gate[1]);
  gate[1] = -1;

  // All 20 bytes fit in the 512-byte pipe. Reading after wait avoids
  // waking the nice-0 parent on every marker and disturbing tie order.
  for(int i = 0; i < count; i++){
    int status, found = 0;
    int pid = wait(&status);
    if(pid < 0)
      goto fail;
    for(int j = 0; j < count; j++){
      if(pids[j] == pid){
        pids[j] = 0;
        found = 1;
        break;
      }
    }
    if(!found || status != 0)
      goto fail;
  }

  for(int i = 0; i < 20; i++){
    if(read(result[0], &order[i], 1) != 1)
      goto fail;
    int worker = order[i] - 'A';
    if(worker < 0 || worker >= 4)
      goto fail;
    counts[worker]++;
    if((i < 10 && worker >= 2) || (i >= 10 && worker < 2))
      bad = 1;
    if(i > 0 && i != 10 && order[i] != order[i - 1])
      changes[i / 10]++;
  }
  if(read(result[0], &extra, 1) != 0)
    goto fail;
  close(result[0]);
  result[0] = -1;

  for(int i = 0; i < 4; i++)
    if(counts[i] != 5)
      bad = 1;
  printf("case 13 progress markers: %s\n", order);
  printf("group precedence and five markers each: %s\n", bad ? "FAIL" : "PASS");
  printf("within-group marker changes: %d, %d\n", changes[0], changes[1]);
  // More than one change rules out two serial blocks. Exact alternation
  // is not guaranteed: a timer can interrupt between uptime and write.
  if(changes[0] < 2 || changes[1] < 2){
    printf("sampled interleaving: insufficient evidence\n");
    bad = 1;
  } else {
    printf("sampled interleaving: observed (not a full scheduler trace)\n");
  }
  return bad;

fail:
  for(int i = 0; i < count; i++)
    if(pids[i] > 0)
      kill(pids[i]);
  for(int i = 0; i < 2; i++){
    if(gate[i] >= 0)
      close(gate[i]);
    if(result[i] >= 0)
      close(result[i]);
  }
  while(wait(0) >= 0)
    ;
  fprintf(2, "case 13 setup, worker, or pipe failure\n");
  return 1;
}

int
main(void)
{
  if(test_priority_groups()){
    fprintf(2, "case 13 diagnostic failed\n");
    exit(1);
  }
  printf("case 13 diagnostic passed\n");
  exit(0);
}
