#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

int
main(int argc, char **argv)
{
  int i;

  // 如果参数少于2个，打印用法并退出
  if(argc < 2){
    fprintf(2, "usage: kill pid...\n");
    exit(1);
  }
  // 遍历所有pid参数
  for(i=1; i<argc; i++)
    // 调用kill系统调用
    kill(atoi(argv[i]));
  exit(0);
}