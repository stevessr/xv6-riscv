#include "kernel/types.h"
#include "kernel/stat.h"
#include "kernel/fcntl.h"
#include "user/user.h"

// 创建一个孤儿文件并检查test-xv6.py是否能恢复它。

#define BUFSZ 500

char buf[BUFSZ];

int
main(int argc, char **argv)
{
  int fd = 0;
  char *s = argv[0];
  struct stat st;
  char *ff = "file0";
  
  // 创建并打开文件
  if ((fd = open(ff, O_CREATE|O_WRONLY)) < 0) {
    printf("%s: open failed\n", s);
    exit(1);
  }
  // 获取文件状态
  if(fstat(fd, &st) < 0){
    fprintf(2, "%s: cannot stat %s\n", s, "ff");
    exit(1);
  }
  // 删除文件，使其成为孤儿文件
  if (unlink(ff) < 0) {
    printf("%s: unlink failed\n", s);
    exit(1);
  }
  // 此时文件应该无法打开
  if (open(ff, O_RDONLY) != -1) {
    printf("%s: open successed\n", s);
    exit(1);
  }
  printf("wait for kill and reclaim %d\n", st.ino);
  // 无限循环直到被杀死
  for(;;) pause(1000);
}