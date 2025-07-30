// 演示将 iderw 中的 "acquire" 移到附加到 idequeue 的循环之后
// 会导致竞争。

// 为了使其工作，您还应该在 iderw 的 idequeue 遍历循环中添加一个自旋。
// 添加以下内容可以在 QEMU 中以 2.1GHz CPU 运行约 5 次 stressfs 后演示一个 panic：
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

  // 创建 4 个子进程
  for(i = 0; i < 4; i++)
    if(fork() > 0)
      break;

  printf("write %d\n", i);

  path[8] += i;
  fd = open(path, O_CREATE | O_RDWR);
  for(i = 0; i < 20; i++)
//    printf(fd, "%d\n", i);
    write(fd, data, sizeof(data));
  close(fd);

  printf("read\n");

  fd = open(path, O_RDONLY);
  for (i = 0; i < 20; i++)
    read(fd, data, sizeof(data));
  close(fd);

  wait(0);

  exit(0);
}
