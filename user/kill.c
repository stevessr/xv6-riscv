// 包含内核数据类型定义
#include "kernel/types.h"
// 包含文件状态信息
#include "kernel/stat.h"
// 包含用户态 API
#include "user/user.h"

// 程序入口
int
main(int argc, char **argv)
{
  int i;

  // 如果参数少于 2 个，则打印用法并退出
  if(argc < 2){
    fprintf(2, "usage: kill pid...\n");
    exit(1);
  }
  // 遍历所有参数（PID）并调用 kill
  for(i=1; i<argc; i++)
    kill(atoi(argv[i]));
  // 正常退出
  exit(0);
}
