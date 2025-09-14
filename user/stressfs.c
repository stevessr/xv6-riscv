// 演示如果在将请求附加到idequeue的循环之后移动iderw中的“acquire”，
// 会导致竞争条件。

// 为了使其工作，您还应该在iderw的idequeue遍历循环中添加一个自旋。
// 添加以下代码在大约5次在2.1GHz CPU的QEMU中运行stressfs后演示了一次panic：
//    for (i = 0; i < 40000; i++)
//      asm volatile("");

#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/fs.h"
#include "kernel/fcntl.h"

int
main(int argc, char *argv[])
{
  int fd, i;
  char path[] = "stressfs0";
  char data[512];

  printf("stressfs starting\n");
  memset(data, 'a', sizeof(data));

  // 创建4个子进程
  for(i = 0; i < 4; i++)
    if(fork() > 0)
      break;

  printf("write %d\n", i);

  // 每个进程使用不同的文件名
  path[8] += i;
  fd = open(path, O_CREATE | O_RDWR);
  // 写入20个块
  for(i = 0; i < 20; i++)
//    printf(fd, "%d\n", i);
    write(fd, data, sizeof(data));
  close(fd);

  printf("read\n");

  // 读取20个块
  fd = open(path, O_RDONLY);
  for (i = 0; i < 20; i++)
    read(fd, data, sizeof(data));
  close(fd);

  wait(0);

  exit(0);
}