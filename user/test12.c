#include "kernel/types.h"
#include "user/user.h"

static int
test_three_worker_round_robin(void)
{
  int gate[2] = {-1, -1}, result[2] = {-1, -1};
  int pids[3], count = 0, reaped = 0;
  int counts[3] = {0, 0, 0}, changes = 0, matches = 0;
  char order[19] = {0}, extra;

  // Stay above the workers until all gate tokens are available (CPUS=1).
  if(setnice(getpid(), 0) < 0)
    goto fail;
  if(pipe(gate) < 0 || pipe(result) < 0)
    goto fail;

  for(int i = 0; i < 3; i++){
    int pid = fork();
    if(pid < 0)
      goto fail;
    if(pid == 0){
      char token, letter = 'A' + i;
      uint tick;
      close(gate[1]);
      close(result[0]);
      if(read(gate[0], &token, 1) != 1)
        exit(1);
      close(gate[0]);
      if(getnice(getpid()) != 10)
        exit(1);

      tick = uptime();
      for(int j = 0; j < 6; j++){
        uint now;
        // Busy work: pause() would make this a sleeping-worker test.
        do {
          now = uptime();
        } while(now == tick);
        tick = now;
        if(write(result[1], &letter, 1) != 1)
          exit(1);
      }
      close(result[1]);
      exit(0);
    }
    pids[count++] = pid;
    if(setnice(pid, 10) < 0 || getnice(pid) != 10)
      goto fail;
  }

  close(gate[0]);
  gate[0] = -1;
  close(result[1]);
  result[1] = -1;
  if(write(gate[1], "xxx", 3) != 3)
    goto fail;
  close(gate[1]);
  gate[1] = -1;

  // All tokens are queued. The parent cannot preempt nice-10 workers.
  // Only 18 bytes are written, so the result pipe need not be drained yet.
  if(setnice(getpid(), 40) < 0)
    goto fail;
  for(int i = 0; i < 18; i++){
    if(read(result[0], &order[i], 1) != 1)
      goto fail;
    if(order[i] < 'A' || order[i] > 'C')
      goto fail;
    counts[order[i] - 'A']++;
    if(i > 0 && order[i] != order[i - 1])
      changes++;
    if(i >= 3 && order[i] == order[i - 3])
      matches++;
  }
  if(read(result[0], &extra, 1) != 0)
    goto fail;
  close(result[0]);
  result[0] = -1;

  while(reaped < count){
    int status, pid = wait(&status), found = 0;
    if(pid < 0)
      goto fail;
    for(int i = 0; i < count; i++){
      if(pids[i] == pid){
        pids[i] = -1;
        found = 1;
        break;
      }
    }
    reaped++;
    if(!found || status != 0)
      goto fail;
  }

  printf("nice 10 markers: %s\n", order);
  printf("counts: A=%d B=%d C=%d, changes: %d\n",
         counts[0], counts[1], counts[2], changes);
  printf("three-place matches: %d/15 (cyclic-progress diagnostic)\n", matches);
  // A tick marker can be delayed across preemption. Neither ABC as the
  // first triple nor a perfect repeating sequence is guaranteed here.
  printf("markers are observations, not a full context-switch trace\n");
  return counts[0] != 6 || counts[1] != 6 || counts[2] != 6 || changes < 3;

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
  return 1;
}

int
main(void)
{
  if(test_three_worker_round_robin()){
    fprintf(2, "test12: worker progress failed\n");
    exit(1);
  }
  printf("test12: worker progress passed\n");
  exit(0);
}
