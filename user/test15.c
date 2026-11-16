#include "kernel/types.h"
#include "user/user.h"

static int
check(char *name, int got, int want)
{
  if(got == want)
    return 0;
  fprintf(2, "FAIL %s: got %d, expected %d\n", name, got, want);
  return 1;
}

static int
reap(int pid, int expected)
{
  int status = 12345;
  int got = wait(&status);
  int bad = check("wait pid", got, pid);

  if(got == pid)
    bad += check("child status", status, expected);
  return bad;
}

static int
test_values(void)
{
  int me = getpid(), bad = 0;

  if(setnice(me, 20) != 0)
    return 1;
  bad += check("INT_MAX rejected", setnice(me, 2147483647), -1);
  bad += check("INT_MAX preserves nice", getnice(me), 20);
  // -INT_MAX is not INT_MIN.
  bad += check("-INT_MAX rejected", setnice(me, -2147483647), -1);
  bad += check("-INT_MAX preserves nice", getnice(me), 20);
  bad += check("same nice succeeds", setnice(me, 20), 0);
  bad += check("same nice remains 20", getnice(me), 20);
  bad += check("get pid 0", getnice(0), -1);
  bad += check("set pid 0", setnice(0, 20), -1);
  bad += check("get pid -5", getnice(-5), -1);
  bad += check("set pid -5", setnice(-5, 20), -1);
  bad += check("invalid pids preserve self", getnice(me), 20);

  int pid = fork();
  if(pid < 0)
    return bad + 1;
  if(pid == 0)
    exit(0);
  bad += reap(pid, 0);
  bad += check("get reaped pid", getnice(pid), -1);
  bad += check("set reaped pid", setnice(pid, 20), -1);
  printf("values: %s\n", bad ? "FAIL" : "PASS");
  return bad;
}

static int
test_inherit(char *self)
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
        char *args[] = {self, "--exec7", 0};
        exec(self, args);
        fprintf(2, "FAIL self exec\n");
        exit(1);
      }
      exit(0);
    }
    // Waiting also lets a nice 40 child run while its parent sleeps.
    bad += reap(pid, 0);
    bad += check("parent nice preserved", getnice(me), values[i]);
    printf("fork nice %d%s: checked child status\n", values[i],
           i == 2 ? " and exec" : "");
  }
  printf("inherit: %s\n", bad ? "FAIL" : "PASS");
  return bad;
}

static int
test_ps(void)
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
  // CPUS=1: after this yield the child (0, then 12) outranks us (20).
  // It has no user-level blocking work before exit. No tick delay is assumed.
  if(setnice(me, 20) != 0){
    kill(pid);
    reap(pid, -1);
    return 1;
  }
  bad += check("zombie nice", getnice(pid), 12);
  printf("PS ZOMBIE BEGIN pid=%d: expect nice 12 zombie\n", pid);
  ps(pid);
  printf("PS ZOMBIE END\n");
  bad += reap(pid, 0);
  bad += check("get after wait", getnice(pid), -1);
  bad += check("set after wait", setnice(pid, 20), -1);
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
    // Stay runnable until killed; no completion timing is asserted.
    for(;;)
      ;
  }
  if(setnice(pid, 30) != 0){
    kill(pid);
    reap(pid, -1);
    return bad + 1;
  }
  bad += check("runnable nice", getnice(pid), 30);
  // CPUS=1: this running nice 0 parent keeps the nice 30 child runnable.
  printf("PS RUNNABLE BEGIN pid=%d: expect nice 30 runble\n", pid);
  ps(pid);
  printf("PS RUNNABLE END\n");
  bad += check("kill runnable child", kill(pid), 0);
  bad += reap(pid, -1);
  printf("ps syscall checks: %s; inspect console rows separately\n",
         bad ? "FAIL" : "PASS");
  return bad;
}

int
main(int argc, char **argv)
{
  int failed = 0, saved = getnice(getpid());
  int all = argc == 1;

  if(argc == 2 && strcmp(argv[1], "--exec7") == 0){
    int value = getnice(getpid());
    printf("exec helper nice: %d\n", value);
    exit(value != 7);
  }
  if(argc > 2 || (argc == 2 && strcmp(argv[1], "values") != 0 &&
     strcmp(argv[1], "inherit") != 0 && strcmp(argv[1], "ps") != 0)){
    fprintf(2, "usage: test15 [values|inherit|ps]\n");
    exit(1);
  }
  if(saved < 0)
    exit(1);
  if(all || strcmp(argv[1], "values") == 0)
    failed += test_values();
  if(all || strcmp(argv[1], "inherit") == 0)
    failed += test_inherit(argv[0]);
  if(all || strcmp(argv[1], "ps") == 0)
    failed += test_ps();
  failed += check("restore nice", setnice(getpid(), saved), 0);
  if(failed){
    fprintf(2, "test15 failed (%d)\n", failed);
    exit(1);
  }
  printf("test15 automated checks passed\n");
  if(all || strcmp(argv[1], "ps") == 0)
    printf("ps display checks require console review; CPUS=1 required\n");
  exit(0);
}
