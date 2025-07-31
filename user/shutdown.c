// 包含内核数据类型定义
#include "kernel/types.h"
// 包含用户态 API
#include "user/user.h"

// 程序入口
int
main(int argc, char *argv[])
{
  // 调用系统关机函数
  shutdown();
  
  // 正常情况下不应该执行到这里，但添加退出代码以确保程序正常结构
  exit(0);
}
