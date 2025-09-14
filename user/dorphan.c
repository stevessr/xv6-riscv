#include "kernel/types.h"
#include "kernel/stat.h"
#include "kernel/fcntl.h"
#include "user/user.h"

// 创建一个孤儿目录并检查test-xv6.py是否能恢复它。

#define BUFSZ 500

char buf[BUFSZ];

int
main(int argc, char **argv)
{
  char *s = argv[0];

  // 创建目录dd
  if(mkdir("dd") != 0){
    printf("%s: mkdir dd failed\n", s);
    exit(1);
  }

  // 切换到目录dd
  if(chdir("dd") != 0){
    printf("%s: chdir dd failed\n", s);
    exit(1);
  }

  // 删除父目录中的dd，使dd成为孤儿目录
  if (unlink("../dd") < 0) {
    printf("%s: unlink failed\n", s);
    exit(1);
  }
  printf("wait for kill and reclaim\n");
  // 无限循环直到被杀死
  for(;;) pause(1000);
}