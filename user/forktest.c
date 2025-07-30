// 测试 fork 是否能正常失败。
// 这是一个很小的可执行文件，因此可以测试进程表被填满的极限情况。

// 包含内核数据类型定义
#include "kernel/types.h"
// 包含文件状态信息
#include "kernel/stat.h"
// 包含用户态 API
#include "user/user.h"

// 定义循环次数
#define N  1000

// 打印字符串
void
print(const char *s)
{
  write(1, s, strlen(s));
}

// fork 测试函数
void
forktest(void)
{
  int n, pid;

  print("fork test\n");

  // 循环创建子进程
  for(n=0; n<N; n++){
    pid = fork();
    if(pid < 0) // fork 失败
      break;
    if(pid == 0) // 子进程直接退出
      exit(0);
  }

  // 如果循环正常结束，说明 fork 声称能够创建 N 个进程
  if(n == N){
    print("fork claimed to work N times!\n");
    exit(1);
  }

  // 等待所有子进程退出
  for(; n > 0; n--){
    if(wait(0) < 0){
      print("wait stopped early\n");
      exit(1);
    }
  }

  // 如果还能 wait 到子进程，说明 wait 的次数不对
  if(wait(0) != -1){
    print("wait got too many\n");
    exit(1);
  }

  print("fork test OK\n");
}

// 程序入口
int
main(void)
{
  forktest();
  exit(0);
}
