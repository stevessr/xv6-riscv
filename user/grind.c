//
// 永远并行运行随机系统调用。
//

#include "kernel/param.h"
#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/fs.h"
#include "kernel/fcntl.h"
#include "kernel/syscall.h"
#include "kernel/memlayout.h"
#include "kernel/riscv.h"

// 来自FreeBSD。
int
do_rand(unsigned long *ctx)
{
/*
 * 计算 x = (7^5 * x) mod (2^31 - 1) 
 * 不会溢出31位：
 *      (2^31 - 1) = 127773 * (7^5) + 2836
 * 来自 "Random number generators: good ones are hard to find",
 * Park and Miller, Communications of the ACM, vol. 31, no. 10,
 * October 1988, p. 1195.
 */
    long hi, lo, x;

    /* 转换为 [1, 0x7ffffffe] 范围。 */
    x = (*ctx % 0x7ffffffe) + 1;
    hi = x / 127773;
    lo = x % 127773;
    x = 16807 * lo - 2836 * hi;
    if (x < 0)
        x += 0x7fffffff;
    /* 转换为 [0, 0x7ffffffd] 范围。 */
    x--;
    *ctx = x;
    return (x);
}

unsigned long rand_next = 1;

int
rand(void)
{
    return (do_rand(&rand_next));
}

void
go(int which_child)
{
  int fd = -1;
  static char buf[999];
  char *break0 = sbrk(0);
  uint64 iters = 0;

  mkdir("grindir");
  if(chdir("grindir") != 0){
    printf("grind: chdir grindir failed\n");
    exit(1);
  }
  chdir("/");
  
  while(1){
    iters++;
    if((iters % 500) == 0)
      write(1, which_child?"B":"A", 1);
    int what = rand() % 23;
    if(what == 1){
      // 测试创建文件
      close(open("grindir/../a", O_CREATE|O_RDWR));
    } else if(what == 2){
      // 测试在多级目录中创建文件
      close(open("grindir/../grindir/../b", O_CREATE|O_RDWR));
    } else if(what == 3){
      // 测试删除文件
      unlink("grindir/../a");
    } else if(what == 4){
      // 测试在子目录中删除文件
      if(chdir("grindir") != 0){
        printf("grind: chdir grindir failed\n");
        exit(1);
      }
      unlink("../b");
      chdir("/");
    } else if(what == 5){
      // 测试打开已存在的文件
      close(fd);
      fd = open("/grindir/../a", O_CREATE|O_RDWR);
    } else if(what == 6){
      // 测试使用.和..路径打开文件
      close(fd);
      fd = open("/./grindir/./../b", O_CREATE|O_RDWR);
    } else if(what == 7){
      // 测试写文件
      write(fd, buf, sizeof(buf));
    } else if(what == 8){
      // 测试读文件
      read(fd, buf, sizeof(buf));
    } else if(what == 9){
      // 测试在目录中创建和删除文件
      mkdir("grindir/../a");
      close(open("a/../a/./a", O_CREATE|O_RDWR));
      unlink("a/a");
    } else if(what == 10){
      // 测试在父目录中创建和删除文件
      mkdir("/../b");
      close(open("grindir/../b/b", O_CREATE|O_RDWR));
      unlink("b/b");
    } else if(what == 11){
      // 测试链接文件
      unlink("b");
      link("../grindir/./../a", "../b");
    } else if(what == 12){
      // 测试链接文件
      unlink("../grindir/../a");
      link(".././b", "/grindir/../a");
    } else if(what == 13){
      // 测试fork和wait
      int pid = fork();
      if(pid == 0){
        exit(0);
      } else if(pid < 0){
        printf("grind: fork failed\n");
        exit(1);
      }
      wait(0);
    } else if(what == 14){
      // 测试多次fork
      int pid = fork();
      if(pid == 0){
        fork();
        fork();
        exit(0);
      } else if(pid < 0){
        printf("grind: fork failed\n");
        exit(1);
      }
      wait(0);
    } else if(what == 15){
      // 测试sbrk增加内存
      sbrk(6011);
    } else if(what == 16){
      // 测试sbrk减少内存
      if(sbrk(0) > break0)
        sbrk(-(sbrk(0) - break0));
    } else if(what == 17){
      // 测试kill
      int pid = fork();
      if(pid == 0){
        close(open("a", O_CREATE|O_RDWR));
        exit(0);
      } else if(pid < 0){
        printf("grind: fork failed\n");
        exit(1);
      }
      if(chdir("../grindir/..") != 0){
        printf("grind: chdir failed\n");
        exit(1);
      }
      kill(pid);
      wait(0);
    } else if(what == 18){
      // 测试自己kill自己
      int pid = fork();
      if(pid == 0){
        kill(getpid());
        exit(0);
      } else if(pid < 0){
        printf("grind: fork failed\n");
        exit(1);
      }
      wait(0);
    } else if(what == 19){
      // 测试管道
      int fds[2];
      if(pipe(fds) < 0){
        printf("grind: pipe failed\n");
        exit(1);
      }
      int pid = fork();
      if(pid == 0){
        fork();
        fork();
        if(write(fds[1], "x", 1) != 1)
          printf("grind: pipe write failed\n");
        char c;
        if(read(fds[0], &c, 1) != 1)
          printf("grind: pipe read failed\n");
        exit(0);
      } else if(pid < 0){
        printf("grind: fork failed\n");
        exit(1);
      }
      close(fds[0]);
      close(fds[1]);
      wait(0);
    } else if(what == 20){
      // 复杂的目录和文件操作
      int pid = fork();
      if(pid == 0){
        unlink("a");
        mkdir("a");
        chdir("a");
        unlink("../a");
        fd = open("x", O_CREATE|O_RDWR);
        unlink("x");
        exit(0);
      } else if(pid < 0){
        printf("grind: fork failed\n");
        exit(1);
      }
      wait(0);
    } else if(what == 21){
      // 检查inode, fd, block的可用性
      unlink("c");
      // 应该总是成功。检查是否有空闲的i节点，
      // 文件描述符，块。
      int fd1 = open("c", O_CREATE|O_RDWR);
      if(fd1 < 0){
        printf("grind: create c failed\n");
        exit(1);
      }
      if(write(fd1, "x", 1) != 1){
        printf("grind: write c failed\n");
        exit(1);
      }
      struct stat st;
      if(fstat(fd1, &st) != 0){
        printf("grind: fstat failed\n");
        exit(1);
      }
      if(st.size != 1){
        printf("grind: fstat reports wrong size %d\n", (int)st.size);
        exit(1);
      }
      if(st.ino > 200){
        printf("grind: fstat reports crazy i-number %d\n", st.ino);
        exit(1);
      }
      close(fd1);
      unlink("c");
    } else if(what == 22){
      // 测试管道和exec: echo hi | cat
      int aa[2], bb[2];
      if(pipe(aa) < 0){
        fprintf(2, "grind: pipe failed\n");
        exit(1);
      }
      if(pipe(bb) < 0){
        fprintf(2, "grind: pipe failed\n");
        exit(1);
      }
      int pid1 = fork();
      if(pid1 == 0){
        close(bb[0]);
        close(bb[1]);
        close(aa[0]);
        close(1);
        if(dup(aa[1]) != 1){
          fprintf(2, "grind: dup failed\n");
          exit(1);
        }
        close(aa[1]);
        char *args[3] = { "echo", "hi", 0 };
        exec("grindir/../echo", args);
        fprintf(2, "grind: echo: not found\n");
        exit(2);
      } else if(pid1 < 0){
        fprintf(2, "grind: fork failed\n");
        exit(3);
      }
      int pid2 = fork();
      if(pid2 == 0){
        close(aa[1]);
        close(bb[0]);
        close(0);
        if(dup(aa[0]) != 0){
          fprintf(2, "grind: dup failed\n");
          exit(4);
        }
        close(aa[0]);
        close(1);
        if(dup(bb[1]) != 1){
          fprintf(2, "grind: dup failed\n");
          exit(5);
        }
        close(bb[1]);
        char *args[2] = { "cat", 0 };
        exec("/cat", args);
        fprintf(2, "grind: cat: not found\n");
        exit(6);
      } else if(pid2 < 0){
        fprintf(2, "grind: fork failed\n");
        exit(7);
      }
      close(aa[0]);
      close(aa[1]);
      close(bb[1]);
      char buf[4] = { 0, 0, 0, 0 };
      read(bb[0], buf+0, 1);
      read(bb[0], buf+1, 1);
      read(bb[0], buf+2, 1);
      close(bb[0]);
      int st1, st2;
      wait(&st1);
      wait(&st2);
      if(st1 != 0 || st2 != 0 || strcmp(buf, "hi\n") != 0){
        printf("grind: exec pipeline failed %d %d \"%s\"\n", st1, st2, buf);
        exit(1);
      }
    }
  }
}

void
iter()
{
  unlink("a");
  unlink("b");
  
  int pid1 = fork();
  if(pid1 < 0){
    printf("grind: fork failed\n");
    exit(1);
  }
  if(pid1 == 0){
    rand_next ^= 31;
    go(0);
    exit(0);
  }

  int pid2 = fork();
  if(pid2 < 0){
    printf("grind: fork failed\n");
    exit(1);
  }
  if(pid2 == 0){
    rand_next ^= 7177;
    go(1);
    exit(0);
  }

  int st1 = -1;
  wait(&st1);
  if(st1 != 0){
    kill(pid1);
    kill(pid2);
  }
  int st2 = -1;
  wait(&st2);

  exit(0);
}

int
main()
{
  while(1){
    int pid = fork();
    if(pid == 0){
      iter();
      exit(0);
    }
    if(pid > 0){
      wait(0);
    }
    pause(20);
    rand_next += 1;
  }
}