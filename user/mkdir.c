#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

int
main(int argc, char *argv[])
{
  int i;

  // 如果参数少于2个，打印用法并退出
  if(argc < 2){
    fprintf(2, "Usage: mkdir files...\n");
    exit(1);
  }

  // 遍历所有要创建的目录名
  for(i = 1; i < argc; i++){
    // 调用mkdir系统调用创建目录
    if(mkdir(argv[i]) < 0){
      fprintf(2, "mkdir: %s failed to create\n", argv[i]);
      break;
    }
  }

  exit(0);
}