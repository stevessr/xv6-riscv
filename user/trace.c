#include "kernel/param.h"
#include "kernel/types.h"
#include "kernel/stat.h"
#include "kernel/syscall.h"
#include "user/user.h"

int
main(int argc, char *argv[])
{
  int i;
  char *nargv[MAXARG];

  if(argc < 3 || (argv[1][0] != '*' && (argv[1][0] < '0' || argv[1][0] > '9'))){
    fprintf(2, "Usage: %s mask command\n", argv[0]);
    fprintf(2, "mask is a bitmask of syscalls to trace, or '*' to trace all. Bits are 1<<SYS_x (see list):\n");
  fprintf(2, "  %2d: fork\n", SYS_fork);
  fprintf(2, "  %2d: exit\n", SYS_exit);
  fprintf(2, "  %2d: wait\n", SYS_wait);
  fprintf(2, "  %2d: pipe\n", SYS_pipe);
  fprintf(2, "  %2d: read\n", SYS_read);
  fprintf(2, "  %2d: kill\n", SYS_kill);
  fprintf(2, "  %2d: exec\n", SYS_exec);
  fprintf(2, "  %2d: fstat\n", SYS_fstat);
  fprintf(2, "  %2d: chdir\n", SYS_chdir);
  fprintf(2, "  %2d: dup\n", SYS_dup);
  fprintf(2, "  %2d: getpid\n", SYS_getpid);
  fprintf(2, "  %2d: sbrk\n", SYS_sbrk);
  fprintf(2, "  %2d: sleep\n", SYS_sleep);
  fprintf(2, "  %2d: uptime\n", SYS_uptime);
  fprintf(2, "  %2d: open\n", SYS_open);
  fprintf(2, "  %2d: write\n", SYS_write);
  fprintf(2, "  %2d: mknod\n", SYS_mknod);
  fprintf(2, "  %2d: unlink\n", SYS_unlink);
  fprintf(2, "  %2d: link\n", SYS_link);
  fprintf(2, "  %2d: mkdir\n", SYS_mkdir);
  fprintf(2, "  %2d: close\n", SYS_close);
  fprintf(2, "  %2d: trace\n", SYS_trace);
    exit(1);
  }

  int mask;
  if(argv[1][0] == '*'){
    mask = -1; // all bits set -> trace all syscalls
  } else {
    mask = atoi(argv[1]);
  }

  if (trace(mask) < 0) {
    fprintf(2, "%s: trace failed\n", argv[0]);
    exit(1);
  }
  
  for(i = 2; i < argc && i < MAXARG; i++){
    nargv[i-2] = argv[i];
  }
  nargv[argc-2] = 0;
  exec(nargv[0], nargv);
  printf("trace: exec failed\n");
  exit(0);
}
