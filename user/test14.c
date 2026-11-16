#include "kernel/types.h"
#include "user/user.h"

#define MARKERS 30

struct event {
  int name;
  uint tick;
};

static int
record(int fd, int name)
{
  struct event e = {name, uptime()};
  return write(fd, &e, sizeof(e)) == sizeof(e) ? 0 : -1;
}

static int
test_sleep_priority(void)
{
  int gate[2] = {-1, -1}, result[2] = {-1, -1};
  int pids[2] = {-1, -1};
  int low = 0, high = 0, before = -1, started = 0;
  uint start = 0, elapsed = 0;
  char order[MARKERS + 2] = {0};
  int used = 0;

  if(setnice(getpid(), 0) < 0 ||
     pipe(gate) < 0 || pipe(result) < 0)
    goto fail;

  for(int i = 0; i < 2; i++){
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
      if(getnice(getpid()) != (i == 0 ? 0 : 20))
        exit(1);

      if(i == 0){
        if(record(result[1], 'S') < 0 || pause(3) < 0 ||
           record(result[1], 'H') < 0)
          exit(1);
      } else {
        for(int j = 0; j < MARKERS; j++){
          uint tick = uptime();
          // CPU work for at least one tick per marker; no voluntary sleep.
          do {
            for(volatile int n = 0; n < 10000; n++)
              ;
          } while((uint)uptime() - tick < 1);
          if(record(result[1], 'L') < 0)
            exit(1);
        }
      }
      close(result[1]);
      exit(0);
    }
    pids[i] = pid;
    if(setnice(pid, i == 0 ? 0 : 20) < 0)
      goto fail;
  }

  close(gate[0]);
  gate[0] = -1;
  close(result[1]);
  result[1] = -1;
  // On CPUS=1, H has priority over L and enters pause before L runs.
  if(write(gate[1], "xx", 2) != 2)
    goto fail;
  close(gate[1]);
  gate[1] = -1;

  // All 32 records fit in the 512-byte pipe. Do not compete while collecting.
  for(int i = 0; i < 2; i++){
    int status, pid = wait(&status);
    int found = 0;
    for(int j = 0; j < 2; j++){
      if(pid > 0 && pid == pids[j]){
        pids[j] = -1;
        found = 1;
      }
    }
    if(!found || status != 0)
      goto fail;
  }

  for(;;){
    struct event e;
    int size = 0;
    while(size < sizeof(e)){
      int n = read(result[0], (char *)&e + size, sizeof(e) - size);
      if(n < 0 || (n == 0 && size != 0))
        goto fail;
      if(n == 0)
        break;
      size += n;
    }
    if(size == 0)
      break;
    if(e.name == 'S'){
      if(started || used != 0)
        goto fail;
      started = 1;
      start = e.tick;
      continue;
    }
    if(!started || used >= MARKERS + 1)
      goto fail;
    if(e.name == 'L'){
      low++;
    } else if(e.name == 'H'){
      if(high++)
        goto fail;
      before = low;
      elapsed = e.tick - start;
    } else {
      goto fail;
    }
    order[used++] = e.name;
  }
  close(result[0]);
  result[0] = -1;

  printf("case 14 progress: %s\n", order);
  printf("H elapsed ticks: %d; L markers before H: %d of %d\n",
         elapsed, before, low);
  printf("markers show progress, not a complete scheduler trace\n");
  // No fixed marker position or claim of asynchronous wakeup preemption.
  return !started || high != 1 || low != MARKERS || elapsed < 3 ||
         before <= 0 || before >= MARKERS;

fail:
  for(int i = 0; i < 2; i++)
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
  if(test_sleep_priority()){
    fprintf(2, "case 14 failed (run with CPUS=1)\n");
    exit(1);
  }
  printf("case 14 passed\n");
  exit(0);
}
