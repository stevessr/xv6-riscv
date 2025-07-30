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
    fprintf(2, "Usage: rm files...\n");
    exit(1);
  }

  // 遍历所有参数（文件名）并删除文件
  for(i = 1; i < argc; i++){
    if(unlink(argv[i]) < 0){
      fprintf(2, "rm: %s failed to delete\n", argv[i]);
      break;
    }
  }

  // 正常退出
  exit(0);
}
