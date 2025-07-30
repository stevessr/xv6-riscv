// 包含内核数据类型定义
#include "kernel/types.h"
// 包含文件控制选项
#include "kernel/fcntl.h"
// 包含用户态 API
#include "user/user.h"

// 缓冲区
char buf[512];

// 从文件描述符 fd 读取数据并打印到标准输出
void
cat(int fd)
{
  int n;

  // 从 fd 读取数据到 buf，最多读取 sizeof(buf) 字节
  while((n = read(fd, buf, sizeof(buf))) > 0) {
    // 将读取到的 n 字节数据写入标准输出
    if (write(1, buf, n) != n) {
      // 写入错误处理
      fprintf(2, "cat: write error\n");
      exit(1);
    }
  }
  // 读取错误处理
  if(n < 0){
    fprintf(2, "cat: read error\n");
    exit(1);
  }
}

// 程序入口
int
main(int argc, char *argv[])
{
  int fd, i;

  // 如果没有命令行参数，则从标准输入读取
  if(argc <= 1){
    cat(0);
    exit(0);
  }

  // 遍历所有命令行参数（文件名）
  for(i = 1; i < argc; i++){
    // 以只读方式打开文件
    if((fd = open(argv[i], O_RDONLY)) < 0){
      // 打开文件失败处理
      fprintf(2, "cat: cannot open %s\n", argv[i]);
      exit(1);
    }
    // 处理文件内容
    cat(fd);
    // 关闭文件
    close(fd);
  }
  // 正常退出
  exit(0);
}
