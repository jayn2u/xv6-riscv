#include "kernel/types.h"
#include "user/user.h"

// 서로 다른 nice의 실행 테스트 코드
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

// 동일한 nice 값의 실행 테스트 코드
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

// nice를 낮춘 자식으로의 즉시 전환
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

// 손자 프로세스의 nice 상속
static int
test_nice_inherited_by_grandchild(void)
{
  int result[2];
  int pid, v = -1, bad = 0;

  if(pipe(result) < 0)
    return 1;
  if(setnice(getpid(), 12) < 0)
    return 1;

  pid = fork();
  if(pid < 0)
    return 1;
  if(pid == 0){
    int gpid;
    close(result[0]);

    if(getnice(getpid()) != 12 || setnice(getpid(), 4) < 0)
      exit(1);

    gpid = fork();
    if(gpid < 0)
      exit(1);
    if(gpid == 0){
      int n = getnice(getpid());
      if(write(result[1], &n, sizeof(n)) != sizeof(n))
        exit(1);
      exit(0);
    }
    close(result[1]);
    if(wait(0) < 0)
      exit(1);
    exit(0);
  }

  close(result[1]);
  if(read(result[0], &v, sizeof(v)) != sizeof(v))
    bad = 1;
  close(result[0]);
  if(wait(0) < 0)
    bad = 1;

  if(getnice(getpid()) != 12)
    bad = 1;
  setnice(getpid(), 0);

  printf("grandchild nice: %d\n", v);
  return bad || v != 4;
}

// 좀비 자식의 nice 조회
static int
test_zombie_nice(void)
{
  int pid, nice, status, bad = 0;

  pid = fork();
  if(pid < 0){
    fprintf(2, "zombie nice: fork failed\n");
    return 1;
  }
  if(pid == 0){
    if(setnice(getpid(), 8) < 0)
      exit(1);
    exit(0);
  }

  if(wait((int *)-1) != -1){
    fprintf(2, "zombie nice: invalid-status wait unexpectedly succeeded\n");
    return 1;
  }

  nice = getnice(pid);
  printf("zombie child nice: %d (expected 8)\n", nice);
  ps(pid);
  if(nice != 8)
    bad = 1;

  if(wait(&status) != pid){
    fprintf(2, "zombie nice: reap failed\n");
    kill(pid);
    while(wait(0) >= 0)
      ;
    return 1;
  }
  if(status != 0){
    fprintf(2, "zombie nice: child status %d (expected 0)\n", status);
    bad = 1;
  }
  return bad;
}

// 실패한 setnice의 반환값과 nice 보존
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
  if(setnice(pid, 5) != 0 || getnice(pid) != 5)
    goto fail;

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
  if(strcmp(order, "PCQ") != 0 && strcmp(order, "CPQ") != 0)
    bad = 1;
  printf("No voluntary yield on failure: unproven without instrumentation.\n");
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

// 같은 우선순위인 세 작업자의 진행
static int
test_three_worker_round_robin(void)
{
  int gate[2] = {-1, -1}, result[2] = {-1, -1};
  int pids[3], count = 0, reaped = 0;
  int counts[3] = {0, 0, 0}, changes = 0, matches = 0;
  char order[19] = {0}, extra;

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

// 서로 다른 우선순위 묶음의 진행
static int
test_priority_groups(void)
{
  int gate[2] = {-1, -1}, result[2] = {-1, -1};
  int pids[4] = {0}, count = 0;
  int values[4] = {5, 5, 15, 15};
  int counts[4] = {0}, changes[2] = {0};
  int bad = 0;
  char order[21] = {0}, extra;

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
  printf("priority groups progress markers: %s\n", order);
  printf("group precedence and five markers each: %s\n", bad ? "FAIL" : "PASS");
  printf("within-group marker changes: %d, %d\n", changes[0], changes[1]);
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
  fprintf(2, "priority groups setup, worker, or pipe failure\n");
  return 1;
}

// 잠들었다 깨어난 높은 우선순위 작업자의 진행
#define LOW_PRIORITY_MARKERS 30

struct sleep_event {
  int name;
  uint tick;
};

static int
record_sleep_event(int fd, int name)
{
  struct sleep_event e = {name, uptime()};
  return write(fd, &e, sizeof(e)) == sizeof(e) ? 0 : -1;
}

static int
test_sleep_priority(void)
{
  int gate[2] = {-1, -1}, result[2] = {-1, -1};
  int pids[2] = {-1, -1};
  int low = 0, high = 0, before = -1, started = 0;
  uint start = 0, elapsed = 0;
  char order[LOW_PRIORITY_MARKERS + 2] = {0};
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
        if(record_sleep_event(result[1], 'S') < 0 || pause(3) < 0 ||
           record_sleep_event(result[1], 'H') < 0)
          exit(1);
      } else {
        for(int j = 0; j < LOW_PRIORITY_MARKERS; j++){
          uint tick = uptime();
          do {
            for(volatile int n = 0; n < 10000; n++)
              ;
          } while((uint)uptime() - tick < 1);
          if(record_sleep_event(result[1], 'L') < 0)
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
  if(write(gate[1], "xx", 2) != 2)
    goto fail;
  close(gate[1]);
  gate[1] = -1;

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
    struct sleep_event e;
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
    if(!started || used >= LOW_PRIORITY_MARKERS + 1)
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

  printf("sleep priority progress: %s\n", order);
  printf("H elapsed ticks: %d; L markers before H: %d of %d\n",
         elapsed, before, low);
  printf("markers show progress, not a complete scheduler trace\n");
  return !started || high != 1 || low != LOW_PRIORITY_MARKERS || elapsed < 3 ||
         before <= 0 || before >= LOW_PRIORITY_MARKERS;

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

// nice 값과 상속 및 프로세스 상태 검사
static int
check_value(char *name, int got, int want)
{
  if(got == want)
    return 0;
  fprintf(2, "FAIL %s: got %d, expected %d\n", name, got, want);
  return 1;
}

static int
reap_child(int pid, int expected)
{
  int status = 12345;
  int got = wait(&status);
  int bad = check_value("wait pid", got, pid);

  if(got == pid)
    bad += check_value("child status", status, expected);
  return bad;
}

static int
test_nice_values(void)
{
  int me = getpid(), bad = 0;

  if(setnice(me, 20) != 0)
    return 1;
  bad += check_value("INT_MAX rejected", setnice(me, 2147483647), -1);
  bad += check_value("INT_MAX preserves nice", getnice(me), 20);
  bad += check_value("-INT_MAX rejected", setnice(me, -2147483647), -1);
  bad += check_value("-INT_MAX preserves nice", getnice(me), 20);
  bad += check_value("same nice succeeds", setnice(me, 20), 0);
  bad += check_value("same nice remains 20", getnice(me), 20);
  bad += check_value("get pid 0", getnice(0), -1);
  bad += check_value("set pid 0", setnice(0, 20), -1);
  bad += check_value("get pid -5", getnice(-5), -1);
  bad += check_value("set pid -5", setnice(-5, 20), -1);
  bad += check_value("invalid pids preserve self", getnice(me), 20);

  int pid = fork();
  if(pid < 0)
    return bad + 1;
  if(pid == 0)
    exit(0);
  bad += reap_child(pid, 0);
  bad += check_value("get reaped pid", getnice(pid), -1);
  bad += check_value("set reaped pid", setnice(pid, 20), -1);
  printf("values: %s\n", bad ? "FAIL" : "PASS");
  return bad;
}

static int
test_fork_exec_nice(void)
{
  int values[3] = {0, 40, 7};
  int me = getpid(), bad = 0;

  for(int i = 0; i < 3; i++){
    if(setnice(me, values[i]) != 0)
      return bad + 1;
    int pid = fork();
    if(pid < 0)
      return bad + 1;
    if(pid == 0){
      if(getnice(getpid()) != values[i])
        exit(1);
      if(i == 2){
        char *args[] = {"test2", "--check-exec-nice", 0};
        exec("/test2", args);
        fprintf(2, "FAIL self exec\n");
        exit(1);
      }
      exit(0);
    }
    bad += reap_child(pid, 0);
    bad += check_value("parent nice preserved", getnice(me), values[i]);
    printf("fork nice %d%s: checked child status\n", values[i],
           i == 2 ? " and exec" : "");
  }
  printf("inherit: %s\n", bad ? "FAIL" : "PASS");
  return bad;
}

static int
test_ps_states(void)
{
  int me = getpid(), bad = 0, pid;

  if(setnice(me, 0) != 0)
    return 1;
  pid = fork();
  if(pid < 0)
    return 1;
  if(pid == 0){
    if(setnice(getpid(), 12) != 0)
      exit(1);
    exit(0);
  }
  if(setnice(me, 20) != 0){
    kill(pid);
    reap_child(pid, -1);
    return 1;
  }
  bad += check_value("zombie nice", getnice(pid), 12);
  printf("PS ZOMBIE BEGIN pid=%d: expect nice 12 zombie\n", pid);
  ps(pid);
  printf("PS ZOMBIE END\n");
  bad += reap_child(pid, 0);
  bad += check_value("get after wait", getnice(pid), -1);
  bad += check_value("set after wait", setnice(pid, 20), -1);
  printf("PS REAPED BEGIN: expect no rows\n");
  ps(pid);
  printf("PS REAPED END\n");
  printf("PS NEGATIVE BEGIN: expect no rows\n");
  ps(-1);
  printf("PS NEGATIVE END\n");

  if(setnice(me, 0) != 0)
    return bad + 1;
  pid = fork();
  if(pid < 0)
    return bad + 1;
  if(pid == 0){
    for(;;)
      ;
  }
  if(setnice(pid, 30) != 0){
    kill(pid);
    reap_child(pid, -1);
    return bad + 1;
  }
  bad += check_value("runnable nice", getnice(pid), 30);
  printf("PS RUNNABLE BEGIN pid=%d: expect nice 30 runble\n", pid);
  ps(pid);
  printf("PS RUNNABLE END\n");
  bad += check_value("kill runnable child", kill(pid), 0);
  bad += reap_child(pid, -1);
  printf("ps syscall checks: %s; inspect console rows separately\n",
         bad ? "FAIL" : "PASS");
  printf("ps display checks require console review; CPUS=1 required\n");
  return bad;
}

// 시나리오별 자식 프로세스 격리 실행
static int
run_test(int (*test)(void), char *name)
{
  int saved = getnice(getpid());
  int status = -1, pid;

  if(saved < 0)
    return 1;
  pid = fork();
  if(pid < 0)
    return 1;
  if(pid == 0){
    if(setnice(getpid(), 0) < 0)
      exit(1);
    exit(test());
  }
  int got = wait(&status);
  int bad = got != pid || status != 0 || getnice(getpid()) != saved;
  printf("%s: %s (child status %d)\n", name, bad ? "FAIL" : "PASS", status);
  return bad;
}

// 전체 테스트 실행
int
main(int argc, char **argv)
{
  int failed = 0;

  if(argc == 2 && strcmp(argv[1], "--check-exec-nice") == 0){
    int value = getnice(getpid());
    printf("exec helper nice: %d\n", value);
    exit(value != 7);
  }

  failed += run_test(test_completion_order, "completion order");
  failed += run_test(test_same_nice_interleave, "same nice interleave");
  failed += run_test(test_lower_nice_switches_immediately, "lower nice switches immediately");
  failed += run_test(test_nice_inherited_by_grandchild, "nice inherited by grandchild");
  failed += run_test(test_zombie_nice, "zombie nice");
  failed += run_test(test_failed_setnice, "failed setnice");
  failed += run_test(test_three_worker_round_robin, "three worker round robin");
  failed += run_test(test_priority_groups, "priority groups");
  failed += run_test(test_sleep_priority, "sleep priority");
  failed += run_test(test_nice_values, "nice values");
  failed += run_test(test_fork_exec_nice, "fork and exec nice");
  failed += run_test(test_ps_states, "ps states");

  if(failed){
    fprintf(2, "test2 tests failed (%d)\n", failed);
    exit(1);
  }
  printf("test2 tests passed\n");
  exit(0);
}
