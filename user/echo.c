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

  // 遍历所有命令行参数
  for(i = 1; i < argc; i++){
    // 将参数写入标准输出
    write(1, argv[i], strlen(argv[i]));
    // 如果不是最后一个参数，则在参数后打印一个空格
    if(i + 1 < argc){
      write(1, " ", 1);
    } else {
      // 如果是最后一个参数，则在参数后打印一个换行符
      write(1, "\n", 1);
    }
  }
  // 正常退出
  exit(0);
}
