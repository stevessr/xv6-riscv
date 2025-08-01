// 包含内核数据类型定义
#include "kernel/types.h"
// 包含文件状态信息
#include "kernel/stat.h"
// 包含用户态 API
#include "user/user.h"

// 程序入口
int
main(int argc, char *argv[])
{
  int i;

  // 如果参数少于 2 个，则打印用法并退出
  if(argc < 2){
    fprintf(2, "用法: mkdir files...\n");
    exit(1);
  }

  // 遍历所有参数（目录名）并创建目录
  for(i = 1; i < argc; i++){
    if(mkdir(argv[i]) < 0){
      fprintf(2, "mkdir: %s 失败 to create\n", argv[i]);
      break;
    }
  }

  // 正常退出
  exit(0);
}
