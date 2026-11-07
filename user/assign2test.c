#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

static int pass = 0, fail = 0;

static void
check(int cond, char *name)
{
  if(cond){
    printf("[PASS] %s\n", name);
    pass++;
  } else {
    printf("[FAIL] %s\n", name);
    fail++;
  }
}

static volatile int sink;

// burn CPU for roughly n "units"
static void
spin(int n)
{
  for(int i = 0; i < n; i++)
    for(int j = 0; j < 1000000; j++)
      sink += j;
}

// ---------- 1. getnice / setnice basics ----------
static void
test_basic(void)
{
  int me = getpid();
  check(getnice(me) == 20, "getnice(self) default is 20");
  check(getnice(99999) == -1, "getnice(nonexistent) == -1");
  check(getnice(0) == -1, "getnice(0) == -1");
  check(getnice(-3) == -1, "getnice(negative) == -1");

  check(setnice(me, 0) == 0, "setnice(self, 0) == 0 (lower bound)");
  check(getnice(me) == 0, "getnice after setnice 0");
  check(setnice(me, 40) == 0, "setnice(self, 40) == 0 (upper bound)");
  check(getnice(me) == 40, "getnice after setnice 40");
  check(setnice(me, -1) == -1, "setnice(self, -1) == -1");
  check(setnice(me, 41) == -1, "setnice(self, 41) == -1");
  check(getnice(me) == 40, "failed setnice leaves value unchanged");
  check(setnice(99999, 10) == -1, "setnice(nonexistent) == -1");
  check(setnice(1, 15) == 0 && getnice(1) == 15, "setnice/getnice on another process (init)");
  setnice(1, 20);
  setnice(me, 20);
}

// ---------- 2. fork inherits nice ----------
static void
test_inherit(void)
{
  int p[2];
  pipe(p);
  setnice(getpid(), 7);
  int pid = fork();
  if(pid == 0){
    int v = getnice(getpid());
    write(p[1], &v, sizeof(v));
    exit(0);
  }
  int v = -1;
  read(p[0], &v, sizeof(v));
  wait(0);
  close(p[0]); close(p[1]);
  check(v == 7, "child inherits parent's nice (7)");
  setnice(getpid(), 20);
}

// ---------- 3. ps ----------
static void
test_ps(void)
{
  printf("--- ps(0) ---\n");
  ps(0);
  printf("--- ps(getpid()) ---\n");
  ps(getpid());
  printf("--- ps(99999): expect nothing ---\n");
  ps(99999);
  printf("--- end ps ---\n");
}

// ---------- 4. strict priority ordering ----------
// Children wait on a gate pipe; parent sets their nice, then opens the gate
// and sleeps in wait(). Each child spins and reports its id when done.
static void
test_priority(void)
{
  int nices[3] = {30, 5, 15};
  int gate[2], res[2], pids[3];
  pipe(gate); pipe(res);
  setnice(getpid(), 0);  // parent outranks children while setting up

  for(int i = 0; i < 3; i++){
    pids[i] = fork();
    if(pids[i] == 0){
      char c;
      read(gate[0], &c, 1);
      spin(150);
      char id = '0' + i;
      write(res[1], &id, 1);
      exit(0);
    }
  }
  for(int i = 0; i < 3; i++)
    setnice(pids[i], nices[i]);
  write(gate[1], "xxx", 3);
  for(int i = 0; i < 3; i++)
    wait(0);

  char order[4] = {0};
  read(res[0], order, 3);
  printf("finish order (child idx): %s  (nice 30,5,15 -> expect 120)\n", order);
  check(order[0] == '1' && order[1] == '2' && order[2] == '0',
        "lower nice finishes first (strict priority)");
  close(gate[0]); close(gate[1]); close(res[0]); close(res[1]);
  setnice(getpid(), 20);
}

// ---------- 5. round-robin among equal nice ----------
// Two equal-nice children write markers after each work chunk.
// RR => markers interleave; FIFO => AAAA...BBBB...
static void
test_rr(void)
{
  int gate[2], res[2];
  pipe(gate); pipe(res);
  setnice(getpid(), 0);
  int pids[2];
  for(int i = 0; i < 2; i++){
    pids[i] = fork();
    if(pids[i] == 0){
      char c;
      read(gate[0], &c, 1);
      char id = 'A' + i;
      for(int k = 0; k < 8; k++){
        spin(40);
        write(res[1], &id, 1);
      }
      exit(0);
    }
  }
  setnice(pids[0], 10);
  setnice(pids[1], 10);
  write(gate[1], "xx", 2);
  wait(0); wait(0);
  char buf[17] = {0};
  read(res[0], buf, 16);
  printf("marker sequence: %s\n", buf);
  int switches = 0;
  for(int i = 1; i < 16; i++)
    if(buf[i] != buf[i-1]) switches++;
  check(switches >= 3, "equal nice processes are interleaved (round-robin)");
  close(gate[0]); close(gate[1]); close(res[0]); close(res[1]);
  setnice(getpid(), 20);
}

// ---------- 6. setnice is a scheduling point ----------
// Parent (nice 10) raises a low-priority runnable child to nice 0.
// The child should run *before* setnice returns to the parent.
static void
test_setnice_preempt(void)
{
  int res[2], gate[2];
  pipe(res); pipe(gate);
  setnice(getpid(), 10);
  int pid = fork();
  if(pid == 0){
    char c;
    read(gate[0], &c, 1);
    write(res[1], "C", 1);
    exit(0);
  }
  setnice(pid, 30);
  write(gate[1], "x", 1);  // child becomes RUNNABLE but nice 30 < parent
  // Parent keeps the CPU (child is lower priority). Give a little time
  // to prove the child does NOT run while parent is higher priority.
  spin(20);
  write(res[1], "P", 1);
  setnice(pid, 0);         // should switch to child immediately
  write(res[1], "Q", 1);
  wait(0);
  char buf[4] = {0};
  read(res[0], buf, 3);
  printf("sequence: %s (expect PCQ)\n", buf);
  check(buf[0] == 'P', "lower-priority child does not preempt parent");
  check(buf[1] == 'C' && buf[2] == 'Q', "setnice() triggers rescheduling");
  close(gate[0]); close(gate[1]); close(res[0]); close(res[1]);
  setnice(getpid(), 20);
}

int
main(void)
{
  test_basic();
  test_inherit();
  test_ps();
  test_priority();
  test_rr();
  test_setnice_preempt();
  printf("RESULT: %d passed, %d failed\n", pass, fail);
  exit(0);
}
