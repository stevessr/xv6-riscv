#include "kernel/types.h"
#include "kernel/fcntl.h"
#include "user/user.h"

char buf[512];

// 将文件描述符fd的内容打印到标准输出
void
cat(int fd)
{
  int n;

  // 从fd读取数据到buf
  while((n = read(fd, buf, sizeof(buf))) > 0) {
    // 将buf中的数据写入标准输出
    if (write(1, buf, n) != n) {
      fprintf(2, "cat: write error\n");
      exit(1);
    }
  }
  // 如果读取出错
  if(n < 0){
    fprintf(2, "cat: read error\n");
    exit(1);
  }
}

int
main(int argc, char *argv[])
{
  int fd, i;

  // 如果没有提供文件名，则从标准输入读取
  if(argc <= 1){
    cat(0);
    exit(0);
  }

  // 遍历所有命令行参数（文件名）
  for(i = 1; i < argc; i++){
    // 打开文件
    if((fd = open(argv[i], O_RDONLY)) < 0){
      fprintf(2, "cat: cannot open %s\n", argv[i]);
      exit(1);
    }
    // 处理文件内容
    cat(fd);
    // 关闭文件
    close(fd);
  }
  exit(0);
}