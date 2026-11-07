#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

// usage: ps          -> print all processes
//        ps pid      -> print only the process with this pid
int
main(int argc, char **argv)
{
  if(argc > 2){
    fprintf(2, "usage: ps [pid]\n");
    exit(1);
  }

  if(argc == 2)
    ps(atoi(argv[1]));
  else
    ps(0);
  exit(0);
}
