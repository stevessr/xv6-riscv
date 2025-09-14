#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

int
main(int argc, char *argv[])
{
  // 如果参数不等于3个，打印用法并退出
  if(argc != 3){
    fprintf(2, "Usage: ln old new\n");
    exit(1);
  }
  // 调用link系统调用创建硬链接
  if(link(argv[1], argv[2]) < 0)
    fprintf(2, "link %s %s: failed\n", argv[1], argv[2]);
  exit(0);
}