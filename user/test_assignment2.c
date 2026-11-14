#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

static void
fail(const char *message, int *gate, int *result, int *pids, int count,
     int original)
{
  fprintf(2, "test_assignment2: %s\n", message);
  for(int i = 0; i < 2; i++){
    if(gate[i] >= 0)
      close(gate[i]);
    if(result[i] >= 0)
      close(result[i]);
  }
  for(int i = 0; i < count; i++)
    if(pids[i] > 0)
      kill(pids[i]);
  while(wait(0) >= 0)
    ;
  if(original >= 0 && setnice(getpid(), original) < 0)
    fprintf(2, "test_assignment2: failed to restore parent nice\n");
  exit(1);
}

int
main(void)
{
  int gate[2] = {-1, -1}, result[2] = {-1, -1};
  int pids[3] = {-1, -1, -1};
  int values[3] = {28, 12, 4};
  char names[3] = {'C', 'B', 'A'};
  char order[4] = {0};
  int count = 0, original = getnice(getpid());

  if(original < 0 || setnice(getpid(), 0) < 0 || getnice(getpid()) != 0)
    fail("cannot prepare parent nice 0", gate, result, pids, count, original);
  if(pipe(gate) < 0 || pipe(result) < 0)
    fail("pipe failed", gate, result, pids, count, original);

  for(int i = 0; i < 3; i++){
    int pid = fork();
    if(pid < 0)
      fail("fork failed", gate, result, pids, count, original);
    if(pid == 0){
      char token, name = names[i];
      close(gate[1]);
      close(result[0]);
      if(read(gate[0], &token, 1) != 1 || token != 'x'){
        close(gate[0]);
        close(result[1]);
        exit(1);
      }
      close(gate[0]);
      volatile uint64 work = 0;
      for(int j = 0; j < 10000000; j++)
        work += j;
      int written = write(result[1], &name, 1);
      close(result[1]);
      exit(written == 1 ? 0 : 1);
    }
    pids[count++] = pid;
    if(setnice(pid, values[i]) < 0 || getnice(pid) != values[i])
      fail("cannot prepare child nice", gate, result, pids, count, original);
  }

  printf("prepared: parent=0, fork C=28 B=12 A=4\n");
  close(gate[0]);
  gate[0] = -1;
  close(result[1]);
  result[1] = -1;
  if(write(gate[1], "xxx", 3) != 3)
    fail("gate write failed", gate, result, pids, count, original);
  close(gate[1]);
  gate[1] = -1;

  int received = 0;
  while(received < 3){
    int n = read(result[0], order + received, 3 - received);
    if(n <= 0)
      fail("missing completion marker", gate, result, pids, count, original);
    received += n;
  }
  close(result[0]);
  result[0] = -1;
  printf("completion order: %s\n", order);

  for(int i = 0; i < count; i++){
    int status = -1, pid = wait(&status), found = 0;
    for(int j = 0; j < count; j++){
      if(pids[j] > 0 && pid == pids[j]){
        pids[j] = -1;
        found = 1;
        break;
      }
    }
    if(!found || status != 0)
      fail("child wait or exit status failed", gate, result, pids, count,
           original);
  }
  if(wait(0) != -1)
    fail("unexpected extra child", gate, result, pids, count, original);
  printf("children exited: 3/3 status 0\n");
  if(strcmp(order, "ABC") != 0)
    fail("expected completion order ABC", gate, result, pids, count, original);
  if(setnice(getpid(), original) < 0 || getnice(getpid()) != original)
    fail("cannot restore parent nice", gate, result, pids, count, original);
  printf("PASS: completion markers ABC; parent nice restored to %d\n", original);
  exit(0);
}
