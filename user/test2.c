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

static int
existing_tests(void)
{
  int failed = 0;

  if(setnice(getpid(), 0) < 0)
    exit(1);

  failed += test_completion_order();
  failed += test_same_nice_interleave();
  failed += test_lower_nice_switches_immediately();
  failed += test_nice_inherited_by_grandchild();

  if(failed){
    fprintf(2, "scheduler test failed (%d)\n", failed);
    exit(1);
  }
  printf("scheduler test passed\n");
  exit(0);
}

// 테스트 10
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

  // kwait는 ZOMBIE 자식을 찾은 뒤에만 종료 상태를 복사한다.
  // 잘못된 주소로 copyout을 실패시켜 freeproc 전에 반환하므로 유일한 자식은 회수되지 않는다.
  // 파이프 데이터나 EOF만으로는 종료 완료를 확인할 수 없다.
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

static int
case10(void)
{
  if(test_zombie_nice()){
    fprintf(2, "test10 failed\n");
    exit(1);
  }
  printf("test10 passed\n");
  exit(0);
}

// 테스트 11
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
  // 성공한 호출은 우선순위가 높은 자식에게 CPU를 양보하고 자식은 게이트에서 잠든다.
  if(setnice(pid, 5) != 0 || getnice(pid) != 5)
    goto fail;

  // 잘못된 호출 전에 게이트를 해제한다. 타이머 선점으로 언제든 C가 기록될 수 있다.
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
  // 파이프는 일부만 읽힐 수 있으므로 표식을 하나씩 읽고 마지막에 EOF를 확인한다.
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
  // C는 Q보다 먼저 기록되어야 한다. C와 P의 순서는 관찰 정보다.
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

static int
case11(void)
{
  printf("test11: run with CPUS=1\n");
  if(test_failed_setnice()){
    fprintf(2, "test11 failed\n");
    exit(1);
  }
  printf("test11 observable checks passed\n");
  printf("No voluntary yield on failure: unproven without instrumentation.\n");
  // PCQ가 나와도 어떤 커널 경로가 실행되었는지는 알 수 없다.
  // 잘못된 호출 이전을 포함해 타이머 선점이 발생하면 CPQ가 나올 수 있다.
  // 이 표식은 개별 사건의 기록이며 전체 스케줄러 실행 기록은 아니다.
  exit(0);
}

// 테스트 12
static int
test_three_worker_round_robin(void)
{
  int gate[2] = {-1, -1}, result[2] = {-1, -1};
  int pids[3], count = 0, reaped = 0;
  int counts[3] = {0, 0, 0}, changes = 0, matches = 0;
  char order[19] = {0}, extra;

  // 시작 신호가 모두 준비될 때까지 부모의 우선순위를 작업자보다 높게 유지한다(CPUS=1).
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
        // CPU를 계속 사용한다. pause()를 쓰면 잠드는 작업자를 검사하게 된다.
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

  // 시작 신호가 모두 파이프에 있다. 부모는 nice가 10인 작업자를 선점할 수 없다.
  // 기록은 총 18바이트이므로 아직 결과 파이프를 읽지 않아도 공간이 충분하다.
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
  // 선점으로 틱 표식의 기록이 늦어질 수 있다. 처음 세 표식이 ABC이거나
  // 전체 표식이 완벽한 반복 순서를 이루는 것은 보장하지 않는다.
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

static int
case12(void)
{
  if(test_three_worker_round_robin()){
    fprintf(2, "test12: worker progress failed\n");
    exit(1);
  }
  printf("test12: worker progress passed\n");
  exit(0);
}

// 테스트 13
static int
test_priority_groups(void)
{
  int gate[2] = {-1, -1}, result[2] = {-1, -1};
  int pids[4] = {0}, count = 0;
  int values[4] = {5, 5, 15, 15};
  int counts[4] = {0}, changes[2] = {0};
  int bad = 0;
  char order[21] = {0}, extra;

  // 준비 중에는 작업자보다 높은 우선순위를 유지하고 이후 wait()에서 잠든다.
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

      // CPU를 계속 사용하여 RUNNABLE 상태를 유지한다. pause나 콘솔 출력은 하지 않는다.
      // 표식은 틱 변화 이후의 진행을 관찰하며 개별 디스패치를 나타내지 않는다.
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

  // 20바이트 모두 512바이트 파이프에 들어간다. wait 이후 읽어서
  // 매 표식마다 nice가 0인 부모를 깨워 동일 우선순위 순서를 방해하지 않는다.
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
  // 변화가 두 번 이상이면 두 작업자가 각각 연속으로 끝난 경우는 배제된다. 정확한 교대는
  // 보장되지 않는다. uptime과 write 사이에 타이머 선점이 발생할 수 있다.
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

static int
case13(void)
{
  if(test_priority_groups()){
    fprintf(2, "case 13 diagnostic failed\n");
    exit(1);
  }
  printf("case 13 diagnostic passed\n");
  exit(0);
}

// 테스트 14
#define CASE14_MARKERS 30

struct case14_event {
  int name;
  uint tick;
};

static int
case14_record(int fd, int name)
{
  struct case14_event e = {name, uptime()};
  return write(fd, &e, sizeof(e)) == sizeof(e) ? 0 : -1;
}

static int
test_sleep_priority(void)
{
  int gate[2] = {-1, -1}, result[2] = {-1, -1};
  int pids[2] = {-1, -1};
  int low = 0, high = 0, before = -1, started = 0;
  uint start = 0, elapsed = 0;
  char order[CASE14_MARKERS + 2] = {0};
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
        if(case14_record(result[1], 'S') < 0 || pause(3) < 0 ||
           case14_record(result[1], 'H') < 0)
          exit(1);
      } else {
        for(int j = 0; j < CASE14_MARKERS; j++){
          uint tick = uptime();
          // 자발적으로 잠들지 않고 표식마다 최소 한 틱 동안 CPU 작업을 한다.
          do {
            for(volatile int n = 0; n < 10000; n++)
              ;
          } while((uint)uptime() - tick < 1);
          if(case14_record(result[1], 'L') < 0)
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
  // CPUS=1에서 H는 L보다 우선순위가 높으므로 L 실행 전에 pause에 들어간다.
  if(write(gate[1], "xx", 2) != 2)
    goto fail;
  close(gate[1]);
  gate[1] = -1;

  // 32개 기록이 모두 512바이트 파이프에 들어간다. 수집 중에는 CPU를 경쟁하지 않는다.
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
    struct case14_event e;
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
    if(!started || used >= CASE14_MARKERS + 1)
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
  // H의 고정 표식 위치나 비동기 깨우기에 의한 즉시 선점을 주장하지 않는다.
  return !started || high != 1 || low != CASE14_MARKERS || elapsed < 3 ||
         before <= 0 || before >= CASE14_MARKERS;

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

static int
case14(void)
{
  if(test_sleep_priority()){
    fprintf(2, "case 14 failed (run with CPUS=1)\n");
    exit(1);
  }
  printf("case 14 passed\n");
  exit(0);
}

// 테스트 15
static int
case15_check(char *name, int got, int want)
{
  if(got == want)
    return 0;
  fprintf(2, "FAIL %s: got %d, expected %d\n", name, got, want);
  return 1;
}

static int
case15_reap(int pid, int expected)
{
  int status = 12345;
  int got = wait(&status);
  int bad = case15_check("wait pid", got, pid);

  if(got == pid)
    bad += case15_check("child status", status, expected);
  return bad;
}

static int
case15_test_values(void)
{
  int me = getpid(), bad = 0;

  if(setnice(me, 20) != 0)
    return 1;
  bad += case15_check("INT_MAX rejected", setnice(me, 2147483647), -1);
  bad += case15_check("INT_MAX preserves nice", getnice(me), 20);
  // -INT_MAX는 INT_MIN이 아니다.
  bad += case15_check("-INT_MAX rejected", setnice(me, -2147483647), -1);
  bad += case15_check("-INT_MAX preserves nice", getnice(me), 20);
  bad += case15_check("same nice succeeds", setnice(me, 20), 0);
  bad += case15_check("same nice remains 20", getnice(me), 20);
  bad += case15_check("get pid 0", getnice(0), -1);
  bad += case15_check("set pid 0", setnice(0, 20), -1);
  bad += case15_check("get pid -5", getnice(-5), -1);
  bad += case15_check("set pid -5", setnice(-5, 20), -1);
  bad += case15_check("invalid pids preserve self", getnice(me), 20);

  int pid = fork();
  if(pid < 0)
    return bad + 1;
  if(pid == 0)
    exit(0);
  bad += case15_reap(pid, 0);
  bad += case15_check("get reaped pid", getnice(pid), -1);
  bad += case15_check("set reaped pid", setnice(pid, 20), -1);
  printf("values: %s\n", bad ? "FAIL" : "PASS");
  return bad;
}

static int
case15_test_inherit(void)
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
        char *args[] = {"test2", "--case15-exec7", 0};
        exec("/test2", args);
        fprintf(2, "FAIL self exec\n");
        exit(1);
      }
      exit(0);
    }
    // 부모가 wait에서 잠들면 nice가 40인 자식도 실행할 수 있다.
    bad += case15_reap(pid, 0);
    bad += case15_check("parent nice preserved", getnice(me), values[i]);
    printf("fork nice %d%s: checked child status\n", values[i],
           i == 2 ? " and exec" : "");
  }
  printf("inherit: %s\n", bad ? "FAIL" : "PASS");
  return bad;
}

static int
case15_test_ps(void)
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
  // CPUS=1에서 양보하면 자식의 nice는 0에서 12가 되어 부모의 20보다 우선한다.
  // 자식은 종료 전에 사용자 수준에서 잠들지 않는다. 고정 틱 지연은 가정하지 않는다.
  if(setnice(me, 20) != 0){
    kill(pid);
    case15_reap(pid, -1);
    return 1;
  }
  bad += case15_check("zombie nice", getnice(pid), 12);
  printf("PS ZOMBIE BEGIN pid=%d: expect nice 12 zombie\n", pid);
  ps(pid);
  printf("PS ZOMBIE END\n");
  bad += case15_reap(pid, 0);
  bad += case15_check("get after wait", getnice(pid), -1);
  bad += case15_check("set after wait", setnice(pid, 20), -1);
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
    // 죽일 때까지 RUNNABLE 상태를 유지한다. 완료 시간은 단정하지 않는다.
    for(;;)
      ;
  }
  if(setnice(pid, 30) != 0){
    kill(pid);
    case15_reap(pid, -1);
    return bad + 1;
  }
  bad += case15_check("runnable nice", getnice(pid), 30);
  // CPUS=1에서 실행 중인 nice가 0인 부모가 nice가 30인 자식을 RUNNABLE로 유지한다.
  printf("PS RUNNABLE BEGIN pid=%d: expect nice 30 runble\n", pid);
  ps(pid);
  printf("PS RUNNABLE END\n");
  bad += case15_check("kill runnable child", kill(pid), 0);
  bad += case15_reap(pid, -1);
  printf("ps syscall checks: %s; inspect console rows separately\n",
         bad ? "FAIL" : "PASS");
  return bad;
}

static int
case15(void)
{
  int failed = 0;

  failed += case15_test_values();
  failed += case15_test_inherit();
  failed += case15_test_ps();
  if(failed){
    fprintf(2, "test15 failed (%d)\n", failed);
    exit(1);
  }
  printf("test15 automated checks passed\n");
  printf("ps display checks require console review; CPUS=1 required\n");
  exit(0);
}

// 각 실행은 별도 자식에서 수행한다. 부모의 nice와 열린 파일은 바뀌지 않는다.
// 부모는 즉시 wait에서 잠들어 작업자와 CPU를 경쟁하지 않는다.
static int
run_case(int number)
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
    switch(number){
    case 0: existing_tests(); break;
    case 10: case10(); break;
    case 11: case11(); break;
    case 12: case12(); break;
    case 13: case13(); break;
    case 14: case14(); break;
    case 15: case15(); break;
    }
    exit(1);
  }
  int got = wait(&status);
  int bad = got != pid || status != 0 || getnice(getpid()) != saved;
  printf("test2 case %d: %s (child status %d)\n",
         number, bad ? "FAIL" : "PASS", status);
  return bad;
}

int
main(int argc, char **argv)
{
  int failed = 0;

  // 테스트 15가 exec한 자식만 이 내부 도우미를 사용한다. 공개 실행 옵션은 없다.
  // 새 프로그램에서는 실행 위치를 기억할 수 없으므로 내부 인자로 도우미를 구분한다.
  // 상속받은 nice를 바꾸거나 전체 테스트를 재실행하기 전에 검사하고 종료한다.
  if(argc == 2 && strcmp(argv[1], "--case15-exec7") == 0){
    int value = getnice(getpid());
    printf("exec helper nice: %d\n", value);
    exit(value != 7);
  }

  failed += run_case(0);
  failed += run_case(10);
  failed += run_case(11);
  failed += run_case(12);
  failed += run_case(13);
  failed += run_case(14);
  failed += run_case(15);

  if(failed){
    fprintf(2, "test2 tests failed (%d)\n", failed);
    exit(1);
  }
  printf("test2 tests passed\n");
  exit(0);
}
