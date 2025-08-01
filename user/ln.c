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
  // 如果参数不等于 3 个，则打印用法并退出
  if(argc != 3){
    fprintf(2, "用法: ln old new\n");
    exit(1);
  }
  // 创建硬链接
  if(link(argv[1], argv[2]) < 0)
    fprintf(2, "link %s %s: 失败\n", argv[1], argv[2]);
  // 正常退出
  exit(0);
}
