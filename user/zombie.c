// Create a zombie process that
// must be reparented at exit.
// 创建一个僵尸进程

#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

int
main(void)
{
  if(fork() > 0)
    pause(5);  // 让子进程先死掉
  exit(0);
}
