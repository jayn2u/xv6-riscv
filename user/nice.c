#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

// usage: nice pid          -> print the nice value of pid
//        nice pid value    -> set the nice value of pid
int
main(int argc, char **argv)
{
  int pid, r;

  if(argc != 2 && argc != 3){
    fprintf(2, "usage: nice pid [value]\n");
    exit(1);
  }

  pid = atoi(argv[1]);
  if(argc == 2){
    r = getnice(pid);
    if(r < 0)
      printf("getnice(%d) failed\n", pid);
    else
      printf("getnice(%d) = %d\n", pid, r);
  } else {
    r = setnice(pid, atoi(argv[2]));
    printf("setnice(%d, %d) = %d\n", pid, atoi(argv[2]), r);
  }
  exit(0);
}
