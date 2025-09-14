#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

int
main(int argc, char *argv[])
{
  int i;

  // 遍历所有命令行参数
  for(i = 1; i < argc; i++){
    // 将参数写入标准输出
    write(1, argv[i], strlen(argv[i]));
    // 如果不是最后一个参数，则打印一个空格
    if(i + 1 < argc){
      write(1, " ", 1);
    } else {
      // 如果是最后一个参数，则打印一个换行符
      write(1, "\n", 1);
    }
  }
  exit(0);
}