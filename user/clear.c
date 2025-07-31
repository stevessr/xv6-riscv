// 包含内核数据类型定义
#include "kernel/types.h"
// 包含用户态 API
#include "user/user.h"

// ANSI 清屏序列
#define CLEAR_SCREEN "\033[2J\033[H"

// 程序入口
int
main(int argc, char *argv[])
{
  // 直接输出 ANSI 清屏控制序列
  write(1, CLEAR_SCREEN, sizeof(CLEAR_SCREEN) - 1);
  
  // 正常退出
  exit(0);
}
