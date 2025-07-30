// 创建一个僵尸进程，
// 该进程在父进程退出时必须被重新认领。

#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

int
main(void)
{
  // fork() 返回值大于 0，说明是父进程
  if(fork() > 0)
    sleep(5);  // 让子进程先于父进程退出
  
  // 子进程直接退出，成为僵尸进程
  // 父进程在 sleep 后退出，init 进程将接管僵尸子进程
  exit(0);
}
