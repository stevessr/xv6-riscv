// 包含内核数据类型定义
#include "kernel/types.h"
// 包含用户态 API
#include "user/user.h"

// 程序入口
int
main(int argc, char *argv[])
{
  // 如果参数不等于 2 个，则打印用法并退出
  if(argc != 2){
    fprintf(2, "用法: sleep seconds\n");
    exit(1);
  }
  // 调用 sleep 系统调用，暂停指定的秒数
  sleep(atoi(argv[1])*10);
  // 正常退出
  exit(0);
}
