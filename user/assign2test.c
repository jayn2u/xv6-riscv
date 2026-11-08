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
  write(gate[1], "x", 1);  // child RUNNABLE, nice 30 > parent nice 10
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

// ---------- 7. self-demotion yields CPU to runnable child ----------
// Parent (nice 10) demotes itself to nice 30 while a child (nice 15) is runnable.
// The child should run before setnice returns to parent.
static void
test_self_demote(void)
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
  setnice(pid, 15);
  write(gate[1], "x", 1); // child is RUNNABLE (nice 15)
  spin(20);
  write(res[1], "P", 1);  // parent writes P (nice 10 < 15, parent keeps CPU)
  setnice(getpid(), 30);  // parent demotes itself to nice 30 -> yields!
  write(res[1], "Q", 1);  // parent writes Q after returning
  wait(0);
  char buf[4] = {0};
  read(res[0], buf, 3);
  printf("sequence: %s (expect PCQ)\n", buf);
  check(buf[0] == 'P' && buf[1] == 'C' && buf[2] == 'Q',
        "self-demotion via setnice yields to higher-priority runnable child");
  close(gate[0]); close(gate[1]); close(res[0]); close(res[1]);
  setnice(getpid(), 20);
}

// ---------- 8. round-robin among 3 equal-nice processes ----------
// Three equal-nice children write markers after each work chunk.
static void
test_rr_three(void)
{
  int gate[2], res[2];
  pipe(gate); pipe(res);
  setnice(getpid(), 0);
  int pids[3];
  for(int i = 0; i < 3; i++){
    pids[i] = fork();
    if(pids[i] == 0){
      char c;
      read(gate[0], &c, 1);
      char id = 'A' + i;
      for(int k = 0; k < 6; k++){
        spin(30);
        write(res[1], &id, 1);
      }
      exit(0);
    }
  }
  for(int i = 0; i < 3; i++)
    setnice(pids[i], 15);
  write(gate[1], "xxx", 3);
  for(int i = 0; i < 3; i++)
    wait(0);
  char buf[19] = {0};
  read(res[0], buf, 18);
  printf("3-proc sequence: %s\n", buf);
  int switches = 0;
  for(int i = 1; i < 18; i++)
    if(buf[i] != buf[i-1]) switches++;
  check(switches >= 6, "3 equal-nice processes interleave across round-robin cycles");
  close(gate[0]); close(gate[1]); close(res[0]); close(res[1]);
  setnice(getpid(), 20);
}

// ---------- 9. grandchild inherits parent's updated nice ----------
static void
test_grandchild(void)
{
  int p[2];
  pipe(p);
  setnice(getpid(), 12);
  int cpid = fork();
  if(cpid == 0){
    // child inherits 12, then sets its nice to 4
    setnice(getpid(), 4);
    int gpid = fork();
    if(gpid == 0){
      // grandchild inherits child's nice (4)
      int v = getnice(getpid());
      write(p[1], &v, sizeof(v));
      exit(0);
    }
    wait(0);
    exit(0);
  }
  int v = -1;
  read(p[0], &v, sizeof(v));
  wait(0);
  close(p[0]); close(p[1]);
  check(v == 4, "grandchild inherits updated parent's nice (4)");
  setnice(getpid(), 20);
}

// ---------- 10. getnice on ZOMBIE process ----------
static void
test_zombie(void)
{
  int p[2];
  pipe(p);
  int cpid = fork();
  if(cpid == 0){
    setnice(getpid(), 8);
    char c = 'x';
    write(p[1], &c, 1);
    exit(0); // child becomes ZOMBIE until parent calls wait()
  }
  char c;
  read(p[0], &c, 1); // wait until child wrote and is exiting
  spin(10);          // give time for child to complete exit()
  int znice = getnice(cpid);
  wait(0);           // reap child
  close(p[0]); close(p[1]);
  check(znice == 8, "getnice on ZOMBIE process returns its nice (8)");
}

// ---------- 11. failed setnice does not yield ----------
static void
test_setnice_fail_no_yield(void)
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
  setnice(pid, 5); // child has higher priority (nice 5)
  write(gate[1], "x", 1); // child becomes RUNNABLE
  // Invalid setnice should fail (-1) and NOT yield
  int r = setnice(getpid(), 45);
  write(res[1], "P", 1);  // parent should still have CPU
  setnice(getpid(), 20);  // valid setnice -> yields to child (nice 5)
  write(res[1], "Q", 1);
  wait(0);
  char buf[4] = {0};
  read(res[0], buf, 3);
  printf("sequence: %s (expect PCQ)\n", buf);
  check(r == -1 && buf[0] == 'P', "failed setnice returns -1 and does not yield");
  close(gate[0]); close(gate[1]); close(res[0]); close(res[1]);
  setnice(getpid(), 20);
}

// spawn n children with the given nice values; each waits on the gate,
// then writes its letter once in every timer tick it gets to run, until it
// has run for `slots` ticks. The marker string is the CPU order per tick.
static void
run_markers(int n, int *nices, char *letters, int slots, char *out, int outlen)
{
  int gate[2], res[2], pids[8];
  pipe(gate); pipe(res);
  setnice(getpid(), 0);
  for(int i = 0; i < n; i++){
    pids[i] = fork();
    if(pids[i] == 0){
      char c;
      read(gate[0], &c, 1);
      int last = -1, done = 0;
      while(done < slots){
        int t = uptime();
        if(t != last){
          write(res[1], &letters[i], 1);
          last = t;
          done++;
        }
      }
      exit(0);
    }
  }
  for(int i = 0; i < n; i++)
    setnice(pids[i], nices[i]);
  write(gate[1], "xxxxxxxx", n);
  for(int i = 0; i < n; i++)
    wait(0);
  int got = 0;
  while(got < outlen - 1 && got < n * slots){
    int r = read(res[0], out + got, n * slots - got);
    if(r <= 0) break;
    got += r;
  }
  out[got] = 0;
  close(gate[0]); close(gate[1]); close(res[0]); close(res[1]);
  setnice(getpid(), 20);
}

// ---------- 12. fixed circular order among three equal-nice processes ----------
// One letter per timer tick: the CPU must rotate as out[i] == out[i-3].
static void
test_rr_cycle(void)
{
  int nices[3] = {10, 10, 10};
  char out[32];
  run_markers(3, nices, "ABC", 6, out, sizeof(out));
  printf("tick sequence: %s\n", out);
  int ok = out[0] != out[1] && out[1] != out[2] && out[0] != out[2];
  for(int i = 3; out[i]; i++)
    if(out[i] != out[i-3]) ok = 0;
  check(ok, "three equal-nice processes run in a fixed circular order");
}

// ---------- 13. two priority groups ----------
// A, B have nice 5; C, D have nice 15. All A/B ticks must come before
// any C/D tick, and each pair must alternate inside its group.
static void
test_groups(void)
{
  int nices[4] = {5, 5, 15, 15};
  char out[32];
  run_markers(4, nices, "ABCD", 5, out, sizeof(out));
  printf("tick sequence: %s\n", out);
  int lastHigh = -1, firstLow = -1, swHigh = 0, swLow = 0;
  char prevHigh = 0, prevLow = 0;
  for(int i = 0; out[i]; i++){
    if(out[i] == 'A' || out[i] == 'B'){
      lastHigh = i;
      if(prevHigh && prevHigh != out[i]) swHigh++;
      prevHigh = out[i];
    } else {
      if(firstLow < 0) firstLow = i;
      if(prevLow && prevLow != out[i]) swLow++;
      prevLow = out[i];
    }
  }
  check(lastHigh < firstLow, "nice 5 group finishes before nice 15 group starts");
  check(swHigh >= 2 && swLow >= 2, "each group alternates inside itself (round-robin)");
}

// ---------- 14. a sleeping high-priority process takes the CPU back ----------
// H (nice 0) sleeps for 3 ticks; L (nice 20) works meanwhile. After H
// wakes, it must get the CPU at a later timer tick, before L finishes.
static void
test_wakeup(void)
{
  int gate[2], res[2], h, l;
  pipe(gate); pipe(res);
  setnice(getpid(), 0);
  h = fork();
  if(h == 0){
    char c;
    read(gate[0], &c, 1);
    pause(3);
    write(res[1], "H", 1);
    exit(0);
  }
  l = fork();
  if(l == 0){
    char c;
    read(gate[0], &c, 1);
    for(int k = 0; k < 30; k++){
      spin(20);
      write(res[1], "L", 1);
    }
    exit(0);
  }
  setnice(h, 0);
  setnice(l, 20);
  write(gate[1], "xx", 2);
  wait(0); wait(0);
  char out[40];
  int got = 0;
  while(got < 31){
    int r = read(res[0], out + got, 31 - got);
    if(r <= 0) break;
    got += r;
  }
  out[got] = 0;
  printf("marker sequence: %s\n", out);
  int pos = -1;
  for(int i = 0; out[i]; i++)
    if(out[i] == 'H') pos = i;
  check(pos > 0 && pos < got - 1, "woken high-priority process preempts the low one");
  close(gate[0]); close(gate[1]); close(res[0]); close(res[1]);
  setnice(getpid(), 20);
}

// ---------- 15. remaining edge cases ----------
static void
test_edges(void)
{
  int me = getpid();

  // pid 0 and negative pids are rejected even with a valid value
  check(setnice(0, 10) == -1 && setnice(-5, 10) == -1, "setnice(0 or negative pid, valid value) == -1");

  // values far outside the range
  check(setnice(me, 2147483647) == -1 && setnice(me, -2147483647) == -1,
        "setnice with INT_MAX / -INT_MAX is rejected");

  // setting the same value succeeds and keeps it
  setnice(me, 20);
  check(setnice(me, 20) == 0 && getnice(me) == 20, "setnice to the same value succeeds");

  // boundary values are inherited too
  int p[2];
  pipe(p);
  int b[2] = {0, 40};
  for(int i = 0; i < 2; i++){
    setnice(me, b[i]);
    int pid = fork();
    if(pid == 0){
      int v = getnice(getpid());
      write(p[1], &v, sizeof(v));
      exit(0);
    }
    int v = -1;
    read(p[0], &v, sizeof(v));
    wait(0);
    check(v == b[i], b[i] == 0 ? "child inherits nice 0" : "child inherits nice 40");
  }
  setnice(me, 20);

  // exec keeps nice: the child execs this program in "nicecheck" mode
  setnice(me, 7);
  int pid = fork();
  if(pid == 0){
    char *argv[] = {"assign2test", "nicecheck", 0};
    exec("assign2test", argv);
    exit(2);
  }
  int st = -1;
  wait(&st);
  check(st == 0, "nice is kept across exec() (7)");
  setnice(me, 20);

  // zombie shows up in ps; after wait() the pid is gone
  pid = fork();
  if(pid == 0){
    setnice(getpid(), 12);
    exit(0);
  }
  pause(2);
  printf("--- ps(zombie child) ---\n");
  ps(pid);
  wait(0);
  check(getnice(pid) == -1 && setnice(pid, 10) == -1, "getnice/setnice on a reaped pid == -1");
  printf("--- ps(reaped pid), ps(-1): expect nothing ---\n");
  ps(pid);
  ps(-1);

  // a RUNNABLE lower-priority child shows up as runble in ps
  int gate[2];
  pipe(gate);
  setnice(me, 0);
  pid = fork();
  if(pid == 0){
    char c;
    read(gate[0], &c, 1);
    spin(5);
    exit(0);
  }
  setnice(pid, 30);
  write(gate[1], "x", 1);
  printf("--- ps(runnable child) ---\n");
  ps(pid);
  wait(0);
  close(gate[0]); close(gate[1]); close(p[0]); close(p[1]);
  setnice(me, 20);
}

int
main(int argc, char **argv)
{
  if(argc == 2 && strcmp(argv[1], "nicecheck") == 0)
    exit(getnice(getpid()) == 7 ? 0 : 1);

  test_basic();
  test_inherit();
  test_ps();
  test_priority();
  test_rr();
  test_setnice_preempt();
  test_self_demote();
  test_rr_three();
  test_grandchild();
  test_zombie();
  test_setnice_fail_no_yield();
  test_rr_cycle();
  test_groups();
  test_wakeup();
  test_edges();
  printf("RESULT: %d passed, %d failed\n", pass, fail);
  exit(0);
}
