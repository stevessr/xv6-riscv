#include "kernel/param.h"    // 包含内核参数头文件，定义了如NPROC、NFILE等常量。
#include "kernel/types.h"    // 包含内核基本类型定义，如uint、uint64等。
#include "kernel/stat.h"     // 包含文件状态结构体和相关宏定义。
#include "user/user.h"       // 包含用户库函数声明，如fork、exit、open、read、write等。
#include "kernel/fs.h"       // 包含文件系统相关常量和结构体，如DIRSIZ、MAXPATH等。
#include "kernel/fcntl.h"    // 包含文件控制选项，如O_CREATE、O_WRONLY、O_RDONLY等。
#include "kernel/syscall.h"  // 包含系统调用号定义，如SYS_fork、SYS_exit等。
#include "kernel/memlayout.h" // 包含内存布局相关常量，如KERNBASE、MAXVA等。
#include "kernel/riscv.h"    // 包含RISC-V架构相关的宏和定义，如PGSIZE、r_sp()等。

//
// 测试xv6系统调用。不带参数运行usertests将运行所有测试，
// usertests <name>将运行指定名称的测试。测试运行器为每个
// 测试创建一个进程，并根据该进程的退出状态报告“OK”或“FAILED”。
// 某些测试可能会导致内核打印usertrap消息，如果测试打印“OK”则可以忽略这些消息。
//

#define BUFSZ  ((MAXOPBLOCKS+2)*BSIZE) // 定义缓冲区大小，用于文件系统测试，通常大于一个块。

char buf[BUFSZ]; // 声明一个全局缓冲区，大小为BUFSZ，用于读写文件等操作。

//
// Section with tests that run fairly quickly.  Use -q if you want to
// run just those.  Without -q usertests also runs the ones that take a
// fair amount of time.
//
// 快速运行测试的章节。如果只想运行这些测试，请使用-q选项。
// 不带-q选项运行usertests也会运行那些耗时较长的测试。
//

// what if you pass ridiculous pointers to system calls
// that read user memory with copyin?
// 如果你向使用copyin读取用户内存的系统调用传递荒谬的指针，会发生什么？
void
copyin(char *s) // copyin测试函数，s是测试名称字符串。
{
  uint64 addrs[] = { 0x80000000LL, 0x3fffffe000, 0x3ffffff000, 0x4000000000,
                     0xffffffffffffffff }; // 定义一系列“荒谬”的地址，包括内核地址、未映射的用户地址、MAXVA附近等。

  for(int ai = 0; ai < sizeof(addrs)/sizeof(addrs[0]); ai++){ // 遍历所有这些地址。
    uint64 addr = addrs[ai]; // 获取当前要测试的地址。

    int fd = open("copyin1", O_CREATE|O_WRONLY); // 创建并打开一个文件，用于写操作。
    if(fd < 0){ // 如果打开失败。
      printf("open(copyin1) failed\n"); // 打印错误信息。
      exit(1); // 退出并报告失败。
    }
    int n = write(fd, (void*)addr, 8192); // 尝试使用这些非法地址作为写入缓冲区的起始地址。
    if(n >= 0){ // 如果写入成功（不应该成功，因为地址非法）。
      printf("write(fd, %p, 8192) returned %d, not -1\n", (void*)addr, n); // 打印错误信息。
      exit(1); // 退出并报告失败。
    }
    close(fd); // 关闭文件。
    unlink("copyin1"); // 删除文件。

    n = write(1, (char*)addr, 8192); // 尝试向标准输出写入，使用非法地址作为源。
    if(n > 0){ // 如果写入成功（不应该成功）。
      printf("write(1, %p, 8192) returned %d, not -1 or 0\n", (void*)addr, n); // 打印错误信息。
      exit(1); // 退出并报告失败。
    }

    int fds[2]; // 声明一个文件描述符数组，用于管道。
    if(pipe(fds) < 0){ // 创建管道。
      printf("pipe() failed\n"); // 如果管道创建失败。
      exit(1); // 退出并报告失败。
    }
    n = write(fds[1], (char*)addr, 8192); // 尝试向管道写入，使用非法地址作为源。
    if(n > 0){ // 如果写入成功（不应该成功）。
      printf("write(pipe, %p, 8192) returned %d, not -1 or 0\n", (void*)addr, n); // 打印错误信息。
      exit(1); // 退出并报告失败。
    }
    close(fds[0]); // 关闭管道读端。
    close(fds[1]); // 关闭管道写端。
  }
}

// 如果你向使用copyout写入用户内存的系统调用传递荒谬的指针，会发生什么？
void
copyout(char *s) // copyout测试函数。
{
  uint64 addrs[] = { 0LL, 0x80000000LL, 0x3fffffe000, 0x3ffffff000, 0x4000000000,
                     0xffffffffffffffff }; // 定义一系列“荒谬”的地址，包括0地址、内核地址、未映射的用户地址、MAXVA附近等。

  for(int ai = 0; ai < sizeof(addrs)/sizeof(addrs[0]); ai++){ // 遍历所有这些地址。
    uint64 addr = addrs[ai]; // 获取当前要测试的地址。

    int fd = open("README", 0); // 打开README文件，用于读操作。
    if(fd < 0){ // 如果打开失败。
      printf("open(README) failed\n"); // 打印错误信息。
      exit(1); // 退出并报告失败。
    }
    int n = read(fd, (void*)addr, 8192); // 尝试从文件读取数据到非法地址。
    if(n > 0){ // 如果读取成功（不应该成功）。
      printf("read(fd, %p, 8192) returned %d, not -1 or 0\n", (void*)addr, n); // 打印错误信息。
      exit(1); // 退出并报告失败。
    }
    close(fd); // 关闭文件。

    int fds[2]; // 声明一个文件描述符数组，用于管道。
    if(pipe(fds) < 0){ // 创建管道。
      printf("pipe() failed\n"); // 如果管道创建失败。
      exit(1); // 退出并报告失败。
    }
    n = write(fds[1], "x", 1); // 向管道写入一个字节。
    if(n != 1){ // 如果写入失败。
      printf("pipe write failed\n"); // 打印错误信息。
      exit(1); // 退出并报告失败。
    }
    n = read(fds[0], (void*)addr, 8192); // 尝试从管道读取数据到非法地址。
    if(n > 0){ // 如果读取成功（不应该成功）。
      printf("read(pipe, %p, 8192) returned %d, not -1 or 0\n", (void*)addr, n); // 打印错误信息。
      exit(1); // 退出并报告失败。
    }
    close(fds[0]); // 关闭管道读端。
    close(fds[1]); // 关闭管道写端。
  }
}

// what if you pass ridiculous string pointers to system calls?
// 如果你向系统调用传递荒谬的字符串指针，会发生什么？
void
copyinstr1(char *s) // copyinstr1测试函数。
{
  uint64 addrs[] = { 0x80000000LL, 0x3fffffe000, 0x3ffffff000, 0x4000000000,
                     0xffffffffffffffff }; // 定义一系列“荒谬”的地址。

  for(int ai = 0; ai < sizeof(addrs)/sizeof(addrs[0]); ai++){ // 遍历所有这些地址。
    uint64 addr = addrs[ai]; // 获取当前要测试的地址。

    int fd = open((char *)addr, O_CREATE|O_WRONLY); // 尝试使用非法地址作为文件名创建并打开文件。
    if(fd >= 0){ // 如果打开成功（不应该成功）。
      printf("open(%p) returned %d, not -1\n", (void*)addr, fd); // 打印错误信息。
      exit(1); // 退出并报告失败。
    }
  }
}

// 如果一个字符串系统调用参数的大小正好是内核缓冲区的大小，
// 导致null终止符正好落在内核缓冲区之外，会发生什么？
void
copyinstr2(char *s) // copyinstr2测试函数。
{
  char b[MAXPATH+1]; // 声明一个字符数组，大小比MAXPATH多一个字节，用于存储文件名。

  for(int i = 0; i < MAXPATH; i++) // 填充数组，使其长度为MAXPATH。
    b[i] = 'x';
  b[MAXPATH] = '\0'; // 将null终止符放在MAXPATH的末尾。

  int ret = unlink(b); // 尝试unlink这个超长文件名。
  if(ret != -1){ // 如果成功（不应该成功）。
    printf("unlink(%s) returned %d, not -1\n", b, ret); // 打印错误信息。
    exit(1); // 退出并报告失败。
  }

  int fd = open(b, O_CREATE | O_WRONLY); // 尝试用这个超长文件名创建文件。
  if(fd != -1){ // 如果成功（不应该成功）。
    printf("open(%s) returned %d, not -1\n", b, fd); // 打印错误信息。
    exit(1); // 退出并报告失败。
  }

  ret = link(b, b); // 尝试将这个超长文件名链接到自身。
  if(ret != -1){ // 如果成功（不应该成功）。
    printf("link(%s, %s) returned %d, not -1\n", b, b, ret); // 打印错误信息。
    exit(1); // 退出并报告失败。
  }

  char *args[] = { "xx", 0 }; // 一个简单的参数列表。
  ret = exec(b, args); // 尝试用这个超长文件名作为程序名执行。
  if(ret != -1){ // 如果成功（不应该成功）。
    printf("exec(%s) returned %d, not -1\n", b, fd); // 打印错误信息。
    exit(1); // 退出并报告失败。
  }

  int pid = fork(); // fork一个子进程。
  if(pid < 0){ // 如果fork失败。
    printf("fork failed\n"); // 打印错误信息。
    exit(1); // 退出并报告失败。
  }
  if(pid == 0){ // 子进程。
    static char big[PGSIZE+1]; // 声明一个比页大小多一个字节的数组。
    for(int i = 0; i < PGSIZE; i++) // 填充数组。
      big[i] = 'x';
    big[PGSIZE] = '\0'; // 将null终止符放在PGSIZE的末尾，使得字符串跨页。
    char *args2[] = { big, big, big, 0 }; // 创建一个参数列表，包含跨页字符串。
    ret = exec("echo", args2); // 尝试exec一个程序，传入跨页字符串作为参数。
    if(ret != -1){ // 如果成功（不应该成功）。
      printf("exec(echo, BIG) returned %d, not -1\n", fd); // 打印错误信息。
      exit(1); // 退出并报告失败。
    }
    exit(747); // OK // 如果exec失败（预期行为），则以747退出。
  }

  int st = 0; // 声明一个变量来存储子进程的退出状态。
  wait(&st); // 等待子进程退出。
  if(st != 747){ // 如果子进程的退出状态不是747。
    printf("exec(echo, BIG) succeeded, should have failed\n"); // 打印错误信息。
    exit(1); // 退出并报告失败。
  }
}

// 如果一个字符串参数跨越了最后一个用户页的末尾，会发生什么？
void
copyinstr3(char *s) // copyinstr3测试函数。
{
  sbrk(8192); // 扩展进程的堆空间8192字节。
  uint64 top = (uint64) sbrk(0); // 获取当前堆的顶部地址。
  if((top % PGSIZE) != 0){ // 如果堆顶部不是页对齐的。
    sbrk(PGSIZE - (top % PGSIZE)); // 调整堆大小使其页对齐。
  }
  top = (uint64) sbrk(0); // 再次获取页对齐后的堆顶部地址。
  if(top % PGSIZE){ // 再次检查是否页对齐（防御性编程）。
    printf("oops\n"); // 打印错误信息。
    exit(1); // 退出并报告失败。
  }

  char *b = (char *) (top - 1); // 将指针设置为最后一个用户页的最后一个字节。
  *b = 'x'; // 写入一个字符到该地址。

  int ret = unlink(b); // 尝试unlink这个指针指向的“文件名”。这个文件名只包含一个字符，但其地址可能在用户空间的末尾。
  if(ret != -1){ // 如果成功（不应该成功）。
    printf("unlink(%s) returned %d, not -1\n", b, ret); // 打印错误信息。
    exit(1); // 退出并报告失败。
  }

  int fd = open(b, O_CREATE | O_WRONLY); // 尝试用这个指针作为文件名创建文件。
  if(fd != -1){ // 如果成功（不应该成功）。
    printf("open(%s) returned %d, not -1\n", b, fd); // 打印错误信息。
    exit(1); // 退出并报告失败。
  }

  ret = link(b, b); // 尝试链接。
  if(ret != -1){ // 如果成功（不应该成功）。
    printf("link(%s, %s) returned %d, not -1\n", b, b, ret); // 打印错误信息。
    exit(1); // 退出并报告失败。
  }

  char *args[] = { "xx", 0 }; // 一个简单的参数列表。
  ret = exec(b, args); // 尝试exec。
  if(ret != -1){ // 如果成功（不应该成功）。
    printf("exec(%s) returned %d, not -1\n", b, fd); // 打印错误信息。
    exit(1); // 退出并报告失败。
  }
}

// 检查内核是否拒绝读/写应用程序不再拥有的用户内存，因为应用程序已将其归还。
void
rwsbrk(char *s) // rwsbrk测试函数。
{
  int fd, n; // 文件描述符和读取/写入字节数。

  uint64 a = (uint64) sbrk(8192); // 扩展堆空间8192字节。

  if(a == (uint64) SBRK_ERROR) { // 如果sbrk失败。
    printf("sbrk(rwsbrk) failed\n"); // 打印错误信息。
    exit(1); // 退出并报告失败。
  }

  if (sbrk(-8192) == SBRK_ERROR) { // 缩减堆空间8192字节，将之前分配的内存归还。
    printf("sbrk(rwsbrk) shrink failed\n"); // 打印错误信息。
    exit(1); // 退出并报告失败。
  }

  fd = open("rwsbrk", O_CREATE|O_WRONLY); // 创建并打开一个文件。
  if(fd < 0){ // 如果打开失败。
    printf("open(rwsbrk) failed\n"); // 打印错误信息。
    exit(1); // 退出并报告失败。
  }
  n = write(fd, (void*)(a+PGSIZE), 1024); // 尝试写入到已经被归还的内存区域 (a+PGSIZE)。
  if(n >= 0){ // 如果写入成功（不应该成功）。
    printf("write(fd, %p, 1024) returned %d, not -1\n", (void*)a+PGSIZE, n); // 打印错误信息。
    exit(1); // 退出并报告失败。
  }
  close(fd); // 关闭文件。
  unlink("rwsbrk"); // 删除文件。

  fd = open("README", O_RDONLY); // 打开README文件。
  if(fd < 0){ // 如果打开失败。
    printf("open(README) failed\n"); // 打印错误信息。
    exit(1); // 退出并报告失败。
  }
  n = read(fd, (void*)(a+PGSIZE), 10); // 尝试从文件读取到已经被归还的内存区域。
  if(n >= 0){ // 如果读取成功（不应该成功）。
    printf("read(fd, %p, 10) returned %d, not -1\n", (void*)a+PGSIZE, n); // 打印错误信息。
    exit(1); // 退出并报告失败。
  }
  close(fd); // 关闭文件。

  exit(0); // 退出并报告成功。
}

// 测试O_TRUNC文件打开模式。
void
truncate1(char *s) // truncate1测试函数。
{
  char buf[32]; // 缓冲区。

  unlink("truncfile"); // 删除可能存在的truncfile文件。
  int fd1 = open("truncfile", O_CREATE|O_WRONLY|O_TRUNC); // 创建并以截断模式打开truncfile。
  write(fd1, "abcd", 4); // 写入"abcd"。
  close(fd1); // 关闭文件描述符fd1。

  int fd2 = open("truncfile", O_RDONLY); // 以只读模式打开truncfile。
  int n = read(fd2, buf, sizeof(buf)); // 从fd2读取数据。
  if(n != 4){ // 应该读取到4个字节。
    printf("%s: read %d bytes, wanted 4\n", s, n); // 打印错误信息。
    exit(1); // 退出并报告失败。
  }

  fd1 = open("truncfile", O_WRONLY|O_TRUNC); // 再次以截断模式打开truncfile (fd1指向的新文件)。

  int fd3 = open("truncfile", O_RDONLY); // 再次以只读模式打开truncfile (fd3指向的新文件)。
  n = read(fd3, buf, sizeof(buf)); // 从fd3读取数据。
  if(n != 0){ // 此时文件内容已被截断，应该读取到0字节。
    printf("aaa fd3=%d\n", fd3); // 调试信息。
    printf("%s: read %d bytes, wanted 0\n", s, n); // 打印错误信息。
    exit(1); // 退出并报告失败。
  }

  n = read(fd2, buf, sizeof(buf)); // 从fd2读取数据。fd2指向的是第一次打开的文件，文件内容已被截断。
  if(n != 0){ // 应该读取到0字节。
    printf("bbb fd2=%d\n", fd2); // 调试信息。
    printf("%s: read %d bytes, wanted 0\n", s, n); // 打印错误信息。
    exit(1); // 退出并报告失败。
  }

  write(fd1, "abcdef", 6); // 通过fd1写入"abcdef"。

  n = read(fd3, buf, sizeof(buf)); // 从fd3读取数据。
  if(n != 6){ // 应该读取到6个字节。
    printf("%s: read %d bytes, wanted 6\n", s, n); // 打印错误信息。
    exit(1); // 退出并报告失败。
  }

  n = read(fd2, buf, sizeof(buf)); // 从fd2读取数据。
  if(n != 2){ // 此时fd2的文件偏移量在之前被截断后没有改变，之后的新写入是从头开始，fd2再读时应该读到“ef”两个字符。
    printf("%s: read %d bytes, wanted 2\n", s, n); // 打印错误信息。
    exit(1); // 退出并报告失败。
  }

  unlink("truncfile"); // 删除文件。

  close(fd1); // 关闭所有文件描述符。
  close(fd2);
  close(fd3);
}

// write to an open FD whose file has just been truncated.
// this causes a write at an offset beyond the end of the file.
// such writes fail on xv6 (unlike POSIX) but at least
// they don't crash.
// 向一个刚被截断的开放文件描述符写入数据。
// 这会导致在文件末尾之外的偏移量处写入。
// 这种写入在xv6中会失败（与POSIX不同），但至少不会崩溃。
void
truncate2(char *s) // truncate2测试函数。
{
  unlink("truncfile"); // 删除可能存在的truncfile文件。

  int fd1 = open("truncfile", O_CREATE|O_TRUNC|O_WRONLY); // 创建并以截断模式打开truncfile。
  write(fd1, "abcd", 4); // 写入"abcd"。

  int fd2 = open("truncfile", O_TRUNC|O_WRONLY); // 再次以截断模式打开truncfile，这将截断fd1指向的文件。

  int n = write(fd1, "x", 1); // 尝试通过fd1写入数据。此时fd1的文件偏移量仍在4，但文件已被fd2截断。
  if(n != -1){ // 在xv6中，这种写入应该失败并返回-1。
    printf("%s: write returned %d, expected -1\n", s, n); // 打印错误信息。
    exit(1); // 退出并报告失败。
  }

  unlink("truncfile"); // 删除文件。
  close(fd1); // 关闭文件描述符。
  close(fd2);
}

void
truncate3(char *s) // truncate3测试函数，并发测试截断和写入。
{
  int pid, xstatus; // 进程ID和退出状态。

  close(open("truncfile", O_CREATE|O_TRUNC|O_WRONLY)); // 创建并清空truncfile。

  pid = fork(); // fork一个子进程。
  if(pid < 0){ // 如果fork失败。
    printf("%s: fork failed\n", s); // 打印错误信息。
    exit(1); // 退出并报告失败。
  }

  if(pid == 0){ // 子进程。
    for(int i = 0; i < 100; i++){ // 循环100次。
      char buf[32]; // 缓冲区。
      int fd = open("truncfile", O_WRONLY); // 以只写模式打开truncfile。
      if(fd < 0){ // 如果打开失败。
        printf("%s: open failed\n", s); // 打印错误信息。
        exit(1); // 退出并报告失败。
      }
      int n = write(fd, "1234567890", 10); // 写入10个字节。
      if(n != 10){ // 如果写入的字节数不等于10。
        printf("%s: write got %d, expected 10\n", s, n); // 打印错误信息。
        exit(1); // 退出并报告失败。
      }
      close(fd); // 关闭文件。
      fd = open("truncfile", O_RDONLY); // 以只读模式打开truncfile。
      read(fd, buf, sizeof(buf)); // 读取文件内容（忽略返回值，只为访问）。
      close(fd); // 关闭文件。
    }
    exit(0); // 子进程成功退出。
  }

  for(int i = 0; i < 150; i++){ // 父进程循环150次。
    int fd = open("truncfile", O_CREATE|O_WRONLY|O_TRUNC); // 创建并以截断模式打开truncfile。
    if(fd < 0){ // 如果打开失败。
      printf("%s: open failed\n", s); // 打印错误信息。
      exit(1); // 退出并报告失败。
    }
    int n = write(fd, "xxx", 3); // 写入3个字节。
    if(n != 3){ // 如果写入的字节数不等于3。
      printf("%s: write got %d, expected 3\n", s, n); // 打印错误信息。
      exit(1); // 退出并报告失败。
    }
    close(fd); // 关闭文件。
  }

  wait(&xstatus); // 等待子进程退出。
  unlink("truncfile"); // 删除文件。
  exit(xstatus); // 以子进程的退出状态退出。
}


// chdir()是否在事务中调用iput(p->cwd)？
void
iputtest(char *s) // iputtest测试函数。
{
  if(mkdir("iputdir") < 0){ // 创建目录iputdir。
    printf("%s: mkdir failed\n", s); // 打印错误信息。
    exit(1); // 退出并报告失败。
  }
  if(chdir("iputdir") < 0){ // 改变当前工作目录到iputdir。
    printf("%s: chdir iputdir failed\n", s); // 打印错误信息。
    exit(1); // 退出并报告失败。
  }
  if(unlink("../iputdir") < 0){ // 删除父目录下的iputdir。此时iputdir是当前工作目录。
    printf("%s: unlink ../iputdir failed\n", s); // 打印错误信息。
    exit(1); // 退出并报告失败。
  }
  if(chdir("/") < 0){ // 改变当前工作目录到根目录。
    printf("%s: chdir / failed\n", s); // 打印错误信息。
    exit(1); // 退出并报告失败。
  }
}

// exit()是否在事务中调用iput(p->cwd)？
void
exitiputtest(char *s) // exitiputtest测试函数。
{
  int pid, xstatus; // 进程ID和退出状态。

  pid = fork(); // fork一个子进程。
  if(pid < 0){ // 如果fork失败。
    printf("%s: fork failed\n", s); // 打印错误信息。
    exit(1); // 退出并报告失败。
  }
  if(pid == 0){ // 子进程。
    if(mkdir("iputdir") < 0){ // 创建目录iputdir。
      printf("%s: mkdir failed\n", s); // 打印错误信息。
      exit(1); // 退出并报告失败。
    }
    if(chdir("iputdir") < 0){ // 改变当前工作目录到iputdir。
      printf("%s: child chdir failed\n", s); // 打印错误信息。
      exit(1); // 退出并报告失败。
    }
    if(unlink("../iputdir") < 0){ // 删除父目录下的iputdir。此时iputdir是当前工作目录。
      printf("%s: unlink ../iputdir failed\n", s); // 打印错误信息。
      exit(1); // 退出并报告失败。
    }
    exit(0); // 子进程退出。
  }
  wait(&xstatus); // 父进程等待子进程退出。
  exit(xstatus); // 父进程以子进程的退出状态退出。
}

// open()尝试写入目录的错误路径是否在事务中调用iput()？
// 需要一个修改过的内核，在sys_open()中的namei()调用之后暂停：
//    if((ip = namei(path)) == 0)
//      return -1;
//    {
//      int i;
//      for(i = 0; i < 10000; i++)
//        yield();
//    }
void
openiputtest(char *s) // openiputtest测试函数。
{
  int pid, xstatus; // 进程ID和退出状态。

  if(mkdir("oidir") < 0){ // 创建目录oidir。
    printf("%s: mkdir oidir failed\n", s); // 打印错误信息。
    exit(1); // 退出并报告失败。
  }
  pid = fork(); // fork一个子进程。
  if(pid < 0){ // 如果fork失败。
    printf("%s: fork failed\n", s); // 打印错误信息。
    exit(1); // 退出并报告失败。
  }
  if(pid == 0){ // 子进程。
    int fd = open("oidir", O_RDWR); // 尝试以读写模式打开目录oidir (应该失败)。
    if(fd >= 0){ // 如果成功（不应该成功）。
      printf("%s: open directory for write succeeded\n", s); // 打印错误信息。
      exit(1); // 退出并报告失败。
    }
    exit(0); // 子进程退出。
  }
  pause(1); // 父进程暂停一段时间，给子进程执行的机会。
  if(unlink("oidir") != 0){ // 删除oidir。
    printf("%s: unlink failed\n", s); // 打印错误信息。
    exit(1); // 退出并报告失败。
  }
  wait(&xstatus); // 父进程等待子进程退出。
  exit(xstatus); // 父进程以子进程的退出状态退出。
}

// simple file system tests
// 简单的文件系统测试

void
opentest(char *s) // opentest测试函数。
{
  int fd; // 文件描述符。

  fd = open("echo", 0); // 打开一个存在的程序文件"echo"。
  if(fd < 0){ // 如果打开失败。
    printf("%s: open echo failed!\n", s); // 打印错误信息。
    exit(1); // 退出并报告失败。
  }
  close(fd); // 关闭文件。
  fd = open("doesnotexist", 0); // 打开一个不存在的文件。
  if(fd >= 0){ // 如果打开成功（不应该成功）。
    printf("%s: open doesnotexist succeeded!\n", s); // 打印错误信息。
    exit(1); // 退出并报告失败。
  }
}

void
writetest(char *s) // writetest测试函数，测试文件写入。
{
  int fd; // 文件描述符。
  int i; // 循环变量。
  enum { N=100, SZ=10 }; // 定义N为100次写入，SZ为每次写入的字节数10。

  fd = open("small", O_CREATE|O_RDWR); // 创建并以读写模式打开文件"small"。
  if(fd < 0){ // 如果打开失败。
    printf("%s: error: creat small failed!\n", s); // 打印错误信息。
    exit(1); // 退出并报告失败。
  }
  for(i = 0; i < N; i++){ // 循环N次。
    if(write(fd, "aaaaaaaaaa", SZ) != SZ){ // 写入"aaaaaaaaaa"。
      printf("%s: error: write aa %d new file failed\n", s, i); // 打印错误信息。
      exit(1); // 退出并报告失败。
    }
    if(write(fd, "bbbbbbbbbb", SZ) != SZ){ // 写入"bbbbbbbbbb"。
      printf("%s: error: write bb %d new file failed\n", s, i); // 打印错误信息。
      exit(1); // 退出并报告失败。
    }
  }
  close(fd); // 关闭文件。
  fd = open("small", O_RDONLY); // 以只读模式打开文件"small"。
  if(fd < 0){ // 如果打开失败。
    printf("%s: error: open small failed!\n", s); // 打印错误信息。
    exit(1); // 退出并报告失败。
  }
  i = read(fd, buf, N*SZ*2); // 读取所有写入的数据。
  if(i != N*SZ*2){ // 如果读取的字节数不等于预期值。
    printf("%s: read failed\n", s); // 打印错误信息。
    exit(1); // 退出并报告失败。
  }
  close(fd); // 关闭文件。

  if(unlink("small") < 0){ // 删除文件"small"。
    printf("%s: unlink small failed\n", s); // 打印错误信息。
    exit(1); // 退出并报告失败。
  }
}

void
writebig(char *s) // writebig测试函数，测试写入大文件。
{
  int i, fd, n; // 循环变量，文件描述符，读取字节数。

  fd = open("big", O_CREATE|O_RDWR); // 创建并以读写模式打开文件"big"。
  if(fd < 0){ // 如果打开失败。
    printf("%s: error: creat big failed!\n", s); // 打印错误信息。
    exit(1); // 退出并报告失败。
  }

  for(i = 0; i < MAXFILE; i++){ // 循环MAXFILE次。
    ((int*)buf)[0] = i; // 在缓冲区头部写入当前循环变量i，用于验证。
    if(write(fd, buf, BSIZE) != BSIZE){ // 写入一个块的数据。
      printf("%s: error: write big file failed i=%d\n", s, i); // 打印错误信息。
      exit(1); // 退出并报告失败。
    }
  }

  close(fd); // 关闭文件。

  fd = open("big", O_RDONLY); // 以只读模式打开文件"big"。
  if(fd < 0){ // 如果打开失败。
    printf("%s: error: open big failed!\n", s); // 打印错误信息。
    exit(1); // 退出并报告失败。
  }

  n = 0; // 初始化已读取块数。
  for(;;){ // 无限循环读取。
    i = read(fd, buf, BSIZE); // 读取一个块的数据。
    if(i == 0){ // 如果读取到文件末尾。
      if(n != MAXFILE){ // 如果读取的块数不等于MAXFILE。
        printf("%s: read only %d blocks from big", s, n); // 打印错误信息。
        exit(1); // 退出并报告失败。
      }
      break; // 跳出循环。
    } else if(i != BSIZE){ // 如果读取的字节数不等于块大小。
      printf("%s: read failed %d\n", s, i); // 打印错误信息。
      exit(1); // 退出并报告失败。
    }
    if(((int*)buf)[0] != n){ // 验证读取的数据是否正确（通过之前写入的i）。
      printf("%s: read content of block %d is %d\n", s,
             n, ((int*)buf)[0]); // 打印错误信息。
      exit(1); // 退出并报告失败。
    }
    n++; // 增加已读取块数。
  }
  close(fd); // 关闭文件。
  if(unlink("big") < 0){ // 删除文件"big"。
    printf("%s: unlink big failed\n", s); // 打印错误信息。
    exit(1); // 退出并报告失败。
  }
}

// many creates, followed by unlink test
// 大量创建文件，然后进行删除测试
void
createtest(char *s) // createtest测试函数。
{
  int i, fd; // 循环变量，文件描述符。
  enum { N=52 }; // 定义创建文件数量为52。

  char name[3]; // 文件名缓冲区。
  name[0] = 'a'; // 文件名第一个字符为'a'。
  name[2] = '\0'; // 文件名终止符。
  for(i = 0; i < N; i++){ // 循环N次。
    name[1] = '0' + i; // 文件名第二个字符从'0'到'9'，再到'A'等等。
    fd = open(name, O_CREATE|O_RDWR); // 创建并打开文件。
    close(fd); // 关闭文件。
  }
  name[0] = 'a'; // 重置文件名。
  name[2] = '\0';
  for(i = 0; i < N; i++){ // 再次循环N次。
    name[1] = '0' + i; // 构建文件名。
    unlink(name); // 删除文件。
  }
}

void dirtest(char *s) // dirtest测试函数，测试目录操作。
{
  if(mkdir("dir0") < 0){ // 创建目录dir0。
    printf("%s: mkdir failed\n", s); // 打印错误信息。
    exit(1); // 退出并报告失败。
  }

  if(chdir("dir0") < 0){ // 改变当前工作目录到dir0。
    printf("%s: chdir dir0 failed\n", s); // 打印错误信息。
    exit(1); // 退出并报告失败。
  }

  if(chdir("..") < 0){ // 改变当前工作目录到父目录。
    printf("%s: chdir .. failed\n", s); // 打印错误信息。
    exit(1); // 退出并报告失败。
  }

  if(unlink("dir0") < 0){ // 删除dir0。
    printf("%s: unlink dir0 failed\n", s); // 打印错误信息。
    exit(1); // 退出并报告失败。
  }
}

void
exectest(char *s) // exectest测试函数，测试exec系统调用。
{
  int fd, xstatus, pid; // 文件描述符，退出状态，进程ID。
  char *echoargv[] = { "echo", "OK", 0 }; // echo程序的参数列表。
  char buf[3]; // 缓冲区。

  unlink("echo-ok"); // 删除可能存在的echo-ok文件。
  pid = fork(); // fork一个子进程。
  if(pid < 0) { // 如果fork失败。
     printf("%s: fork failed\n", s); // 打印错误信息。
     exit(1); // 退出并报告失败。
  }
  if(pid == 0) { // 子进程。
    close(1); // 关闭标准输出。
    fd = open("echo-ok", O_CREATE|O_WRONLY); // 创建并以只写模式打开echo-ok文件。
    if(fd < 0) { // 如果打开失败。
      printf("%s: create failed\n", s); // 打印错误信息。
      exit(1); // 退出并报告失败。
    }
    if(fd != 1) { // 如果文件描述符不是1（重定向到标准输出）。
      printf("%s: wrong fd\n", s); // 打印错误信息。
      exit(1); // 退出并报告失败。
    }
    if(exec("echo", echoargv) < 0){ // 执行echo程序。
      printf("%s: exec echo failed\n", s); // 打印错误信息。
      exit(1); // 退出并报告失败。
    }
    // won't get to here // exec成功后，这里不会被执行。
  }
  if (wait(&xstatus) != pid) { // 父进程等待子进程退出。
    printf("%s: wait failed!\n", s); // 打印错误信息。
  }
  if(xstatus != 0) // 如果子进程退出状态不为0。
    exit(xstatus); // 以子进程的退出状态退出。

  fd = open("echo-ok", O_RDONLY); // 以只读模式打开echo-ok文件。
  if(fd < 0) { // 如果打开失败。
    printf("%s: open failed\n", s); // 打印错误信息。
    exit(1); // 退出并报告失败。
  }
  if (read(fd, buf, 2) != 2) { // 读取2个字节到缓冲区。
    printf("%s: read failed\n", s); // 打印错误信息。
    exit(1); // 退出并报告失败。
  }
  unlink("echo-ok"); // 删除文件。
  if(buf[0] == 'O' && buf[1] == 'K') // 验证读取的内容是否为"OK"。
    exit(0); // 成功退出。
  else {
    printf("%s: wrong output\n", s); // 打印错误信息。
    exit(1); // 退出并报告失败。
  }
}

// 简单的fork和管道读写

void
pipe1(char *s) // pipe1测试函数，测试管道通信。
{
  int fds[2], pid, xstatus; // 文件描述符数组，进程ID，退出状态。
  int seq, i, n, cc, total; // 序列号，循环变量，读取字节数，当前尝试读取的字节数，总字节数。
  enum { N=5, SZ=1033 }; // 定义写入N次，每次写入SZ字节。

  if(pipe(fds) != 0){ // 创建管道。
    printf("%s: pipe() failed\n", s); // 如果创建失败。
    exit(1); // 退出并报告失败。
  }
  pid = fork(); // fork一个子进程。
  seq = 0; // 初始化序列号。
  if(pid == 0){ // 子进程（写入方）。
    close(fds[0]); // 关闭管道的读端。
    for(n = 0; n < N; n++){ // 循环N次写入。
      for(i = 0; i < SZ; i++) // 填充缓冲区，使用序列号。
        buf[i] = seq++;
      if(write(fds[1], buf, SZ) != SZ){ // 写入SZ字节到管道。
        printf("%s: pipe1 oops 1\n", s); // 打印错误信息。
        exit(1); // 退出并报告失败。
      }
    }
    exit(0); // 子进程成功退出。
  } else if(pid > 0){ // 父进程（读取方）。
    close(fds[1]); // 关闭管道的写端。
    total = 0; // 初始化总读取字节数。
    cc = 1; // 初始化当前尝试读取的字节数。
    while((n = read(fds[0], buf, cc)) > 0){ // 循环从管道读取数据。
      for(i = 0; i < n; i++){ // 遍历读取的字节。
        if((buf[i] & 0xff) != (seq++ & 0xff)){ // 验证读取的数据是否正确（与序列号匹配）。
          printf("%s: pipe1 oops 2\n", s); // 打印错误信息。
          return; // 返回。
        }
      }
      total += n; // 累加总读取字节数。
      cc = cc * 2; // 尝试读取的字节数翻倍。
      if(cc > sizeof(buf)) // 如果超过缓冲区大小。
        cc = sizeof(buf); // 限制为缓冲区大小。
    }
    if(total != N * SZ){ // 如果总读取字节数不等于预期值。
      printf("%s: pipe1 oops 3 total %d\n", s, total); // 打印错误信息。
      exit(1); // 退出并报告失败。
    }
    close(fds[0]); // 关闭管道读端。
    wait(&xstatus); // 等待子进程退出。
    exit(xstatus); // 以子进程的退出状态退出。
  } else { // fork失败。
    printf("%s: fork() failed\n", s); // 打印错误信息。
    exit(1); // 退出并报告失败。
  }
}


// 测试子进程是否被杀死 (状态 = -1)
void
killstatus(char *s) // killstatus测试函数。
{
  int xst; // 退出状态。

  for(int i = 0; i < 100; i++){ // 循环100次。
    int pid1 = fork(); // fork一个子进程。
    if(pid1 < 0){ // 如果fork失败。
      printf("%s: fork failed\n", s); // 打印错误信息。
      exit(1); // 退出并报告失败。
    }
    if(pid1 == 0){ // 子进程。
      while(1) { // 进入无限循环。
        getpid(); // 持续调用getpid，保持活跃。
      }
      exit(0); // 不应该到达这里。
    }
    pause(1); // 父进程暂停1个时间单位，给子进程运行的机会。
    kill(pid1); // 杀死子进程。
    wait(&xst); // 等待子进程退出。
    if(xst != -1) { // 如果退出状态不是-1（被杀死）。
       printf("%s: status should be -1\n", s); // 打印错误信息。
       exit(1); // 退出并报告失败。
    }
  }
  exit(0); // 成功退出。
}

// 旨在在最多两个CPU上运行
void
preempt(char *s) // preempt测试函数，测试抢占。
{
  int pid1, pid2, pid3; // 三个子进程的ID。
  int pfds[2]; // 管道文件描述符。

  pid1 = fork(); // fork第一个子进程。
  if(pid1 < 0) { // 如果fork失败。
    printf("%s: fork failed", s); // 打印错误信息。
    exit(1); // 退出并报告失败。
  }
  if(pid1 == 0) // 第一个子进程。
    for(;;) // 进入无限循环，消耗CPU。
      ;

  pid2 = fork(); // fork第二个子进程。
  if(pid2 < 0) { // 如果fork失败。
    printf("%s: fork failed\n", s); // 打印错误信息。
    exit(1); // 退出并报告失败。
  }
  if(pid2 == 0) // 第二个子进程。
    for(;;) // 进入无限循环，消耗CPU。
      ;

  pipe(pfds); // 创建管道。
  pid3 = fork(); // fork第三个子进程。
  if(pid3 < 0) { // 如果fork失败。
     printf("%s: fork failed\n", s); // 打印错误信息。
     exit(1); // 退出并报告失败。
  }
  if(pid3 == 0){ // 第三个子进程。
    close(pfds[0]); // 关闭管道读端。
    if(write(pfds[1], "x", 1) != 1) // 向管道写入一个字节。
      printf("%s: preempt write error", s); // 打印错误信息。
    close(pfds[1]); // 关闭管道写端。
    for(;;) // 进入无限循环，消耗CPU。
      ;
  }

  close(pfds[1]); // 父进程关闭管道写端。
  if(read(pfds[0], buf, sizeof(buf)) != 1){ // 从管道读取一个字节。
    printf("%s: preempt read error", s); // 打印错误信息。
    return; // 返回。
  }
  close(pfds[0]); // 关闭管道读端。
  printf("kill... "); // 打印信息。
  kill(pid1); // 杀死第一个子进程。
  kill(pid2); // 杀死第二个子进程。
  kill(pid3); // 杀死第三个子进程。
  printf("wait... "); // 打印信息。
  wait(0); // 等待所有子进程退出。
  wait(0);
  wait(0);
}

// 尝试查找exit和wait之间的任何竞态条件
void
exitwait(char *s) // exitwait测试函数。
{
  int i, pid; // 循环变量，进程ID。

  for(i = 0; i < 100; i++){ // 循环100次。
    pid = fork(); // fork一个子进程。
    if(pid < 0){ // 如果fork失败。
      printf("%s: fork failed\n", s); // 打印错误信息。
      exit(1); // 退出并报告失败。
    }
    if(pid){ // 父进程。
      int xstate; // 退出状态。
      if(wait(&xstate) != pid){ // 等待子进程退出，并检查返回的PID是否正确。
        printf("%s: wait wrong pid\n", s); // 打印错误信息。
        exit(1); // 退出并报告失败。
      }
      if(i != xstate) { // 检查子进程的退出状态是否正确。
        printf("%s: wait wrong exit status\n", s); // 打印错误信息。
        exit(1); // 退出并报告失败。
      }
    } else { // 子进程。
      exit(i); // 以当前循环变量i作为退出状态退出。
    }
  }
}

// 尝试查找当父进程在其子进程仍然存活时退出，
// 重新分配父进程的代码中的竞态条件。
void
reparent(char *s) // reparent测试函数。
{
  int master_pid = getpid(); // 获取主进程的PID。
  for(int i = 0; i < 200; i++){ // 循环200次。
    int pid = fork(); // fork一个子进程。
    if(pid < 0){ // 如果fork失败。
      printf("%s: fork failed\n", s); // 打印错误信息。
      exit(1); // 退出并报告失败。
    }
    if(pid){ // 父进程。
      if(wait(0) != pid){ // 等待子进程退出。
        printf("%s: wait wrong pid\n", s); // 打印错误信息。
        exit(1); // 退出并报告失败。
      }
    } else { // 子进程。
      int pid2 = fork(); // 子进程再fork一个孙子进程。
      if(pid2 < 0){ // 如果fork失败。
        kill(master_pid); // 杀死主进程。
        exit(1); // 退出并报告失败。
      }
      exit(0); // 子进程退出，其子进程将被init进程收养。
    }
  }
  exit(0); // 成功退出。
}

// 如果两个子进程同时exit()，会发生什么？
void
twochildren(char *s) // twochildren测试函数。
{
  for(int i = 0; i < 1000; i++){ // 循环1000次。
    int pid1 = fork(); // fork第一个子进程。
    if(pid1 < 0){ // 如果fork失败。
      printf("%s: fork failed\n", s); // 打印错误信息。
      exit(1); // 退出并报告失败。
    }
    if(pid1 == 0){ // 第一个子进程。
      exit(0); // 退出。
    } else { // 父进程。
      int pid2 = fork(); // fork第二个子进程。
      if(pid2 < 0){ // 如果fork失败。
        printf("%s: fork failed\n", s); // 打印错误信息。
        exit(1); // 退出并报告失败。
      }
      if(pid2 == 0){ // 第二个子进程。
        exit(0); // 退出。
      } else { // 父进程。
        wait(0); // 等待第一个子进程。
        wait(0); // 等待第二个子进程。
      }
    }
  }
}

// 并发fork以尝试暴露锁的bug。
void
forkfork(char *s) // forkfork测试函数。
{
  enum { N=2 }; // 定义N为2个父进程。

  for(int i = 0; i < N; i++){ // 循环N次，创建N个父进程。
    int pid = fork(); // fork一个子进程。
    if(pid < 0){ // 如果fork失败。
      printf("%s: fork failed", s); // 打印错误信息。
      exit(1); // 退出并报告失败。
    }
    if(pid == 0){ // 子进程。
      for(int j = 0; j < 200; j++){ // 子进程再循环200次。
        int pid1 = fork(); // 子进程fork孙子进程。
        if(pid1 < 0){ // 如果fork失败。
          exit(1); // 退出并报告失败。
        }
        if(pid1 == 0){ // 孙子进程。
          exit(0); // 退出。
        }
        wait(0); // 子进程等待孙子进程退出。
      }
      exit(0); // 子进程成功退出。
    }
  }

  int xstatus; // 退出状态。
  for(int i = 0; i < N; i++){ // 主进程等待N个子进程退出。
    wait(&xstatus); // 等待子进程退出。
    if(xstatus != 0) { // 如果退出状态不为0。
      printf("%s: fork in child failed", s); // 打印错误信息。
      exit(1); // 退出并报告失败。
    }
  }
}

void
forkforkfork(char *s) // forkforkfork测试函数，创建大量进程以耗尽资源。
{
  unlink("stopforking"); // 删除可能存在的stopforking文件。

  int pid = fork(); // fork一个子进程。
  if(pid < 0){ // 如果fork失败。
    printf("%s: fork failed", s); // 打印错误信息。
    exit(1); // 退出并报告失败。
  }
  if(pid == 0){ // 子进程。
    while(1){ // 进入无限循环。
      int fd = open("stopforking", 0); // 尝试打开stopforking文件。
      if(fd >= 0){ // 如果打开成功。
        exit(0); // 退出循环，表示停止fork。
      }
      if(fork() < 0){ // 尝试fork新进程，如果失败。
        close(open("stopforking", O_CREATE|O_RDWR)); // 创建stopforking文件，通知父进程停止。
      }
    }

    exit(0); // 子进程退出。
  }

  pause(20); // 主进程暂停20个时间单位 (2秒)。
  close(open("stopforking", O_CREATE|O_RDWR)); // 创建stopforking文件，确保子进程停止。
  wait(0); // 等待子进程退出。
  pause(10); // 主进程暂停10个时间单位 (1秒)。
}

// 回归测试。当将子进程交给init时，reparent()是否违反了父进程优先于子进程的锁定顺序，
// 从而导致exit()与init的wait()死锁？也用于触发“panic: release”错误，
// 原因在于exit()释放了与获取时不同的p->parent->lock。
void
reparent2(char *s) // reparent2测试函数。
{
  for(int i = 0; i < 800; i++){ // 循环800次。
    int pid1 = fork(); // fork一个子进程。
    if(pid1 < 0){ // 如果fork失败。
      printf("fork failed\n"); // 打印错误信息。
      exit(1); // 退出并报告失败。
    }
    if(pid1 == 0){ // 子进程。
      fork(); // 子进程再fork一个。
      fork(); // 子进程再fork一个。 (总共一个父进程，一个子进程，两个孙子进程)
      exit(0); // 子进程退出。
    }
    wait(0); // 父进程等待子进程退出。
  }

  exit(0); // 成功退出。
}

// allocate all mem, free it, and allocate again
// 分配所有内存，释放它，然后再次分配
void
mem(char *s) // mem测试函数，测试内存分配与释放。
{
  void *m1, *m2; // 内存指针。
  int pid; // 进程ID。

  if((pid = fork()) == 0){ // fork一个子进程。
    m1 = 0; // 初始化m1为0。
    while((m2 = malloc(10001)) != 0){ // 循环分配10001字节的内存，直到分配失败。
      *(char**)m2 = m1; // 将前一个分配的地址存储在当前分配的内存块头部，形成链表。
      m1 = m2; // 更新m1为当前分配的地址。
    }
    while(m1){ // 遍历链表，释放所有分配的内存。
      m2 = *(char**)m1; // 获取下一个内存块的地址。
      free(m1); // 释放当前内存块。
      m1 = m2; // 移动到下一个内存块。
    }
    m1 = malloc(1024*20); // 再次分配20KB内存。
    if(m1 == 0){ // 如果分配失败。
      printf("%s: couldn't allocate mem?!!\n", s); // 打印错误信息。
      exit(1); // 退出并报告失败。
    }
    free(m1); // 释放内存。
    exit(0); // 成功退出。
  } else { // 父进程。
    int xstatus; // 退出状态。
    wait(&xstatus); // 等待子进程退出。
    if(xstatus == -1){ // 如果子进程因页面错误被杀死。
      // probably page fault, so might be lazy lab,
      // so OK.
      // 可能是页面错误，因此可能是惰性分配实验，所以OK。
      exit(0); // 成功退出。
    }
    exit(xstatus); // 以子进程的退出状态退出。
  }
}

// 更多文件系统测试

// 两个进程写入同一个文件描述符
// 偏移量是否共享？inode锁定是否有效？
void
sharedfd(char *s) // sharedfd测试函数。
{
  int fd, pid, i, n, nc, np; // 文件描述符，进程ID，循环变量，读取字节数，子进程写入计数，父进程写入计数。
  enum { N = 1000, SZ=10}; // 定义写入N次，每次SZ字节。
  char buf[SZ]; // 缓冲区。

  unlink("sharedfd"); // 删除可能存在的sharedfd文件。
  fd = open("sharedfd", O_CREATE|O_RDWR); // 创建并以读写模式打开sharedfd文件。
  if(fd < 0){ // 如果打开失败。
    printf("%s: cannot open sharedfd for writing", s); // 打印错误信息。
    exit(1); // 退出并报告失败。
  }
  pid = fork(); // fork一个子进程。
  memset(buf, pid==0?'c':'p', sizeof(buf)); // 根据是子进程还是父进程，用'c'或'p'填充缓冲区。
  for(i = 0; i < N; i++){ // 循环N次写入。
    if(write(fd, buf, sizeof(buf)) != sizeof(buf)){ // 写入数据。
      printf("%s: write sharedfd failed\n", s); // 打印错误信息。
      exit(1); // 退出并报告失败。
    }
  }
  if(pid == 0) { // 子进程。
    exit(0); // 成功退出。
  } else { // 父进程。
    int xstatus; // 退出状态。
    wait(&xstatus); // 等待子进程退出。
    if(xstatus != 0) // 如果子进程退出状态不为0。
      exit(xstatus); // 以子进程的退出状态退出。
  }

  close(fd); // 关闭文件。
  fd = open("sharedfd", 0); // 以只读模式打开sharedfd文件。
  if(fd < 0){ // 如果打开失败。
    printf("%s: cannot open sharedfd for reading\n", s); // 打印错误信息。
    exit(1); // 退出并报告失败。
  }
  nc = np = 0; // 初始化计数器。
  while((n = read(fd, buf, sizeof(buf))) > 0){ // 循环读取文件内容。
    for(i = 0; i < sizeof(buf); i++){ // 遍历读取的字节。
      if(buf[i] == 'c') // 如果是子进程写入的字符。
        nc++; // 增加子进程写入计数。
      if(buf[i] == 'p') // 如果是父进程写入的字符。
        np++; // 增加父进程写入计数。
    }
  }
  close(fd); // 关闭文件。
  unlink("sharedfd"); // 删除文件。
  if(nc == N*SZ && np == N*SZ){ // 验证子进程和父进程写入的字符数量是否正确。
    exit(0); // 成功退出。
  } else {
    printf("%s: nc/np test fails\n", s); // 打印错误信息。
    exit(1); // 退出并报告失败。
  }
}

// 四个进程同时写入不同的文件，以测试块分配。
void
fourfiles(char *s) // fourfiles测试函数。
{
  int fd, pid, i, j, n, total, pi; // 文件描述符，进程ID，循环变量，读取字节数，总字节数，进程索引。
  char *names[] = { "f0", "f1", "f2", "f3" }; // 文件名数组。
  char *fname; // 当前文件名。
  enum { N=12, NCHILD=4, SZ=500 }; // 定义每个文件写入N次，子进程数量NCHILD，每次写入SZ字节。

  for(pi = 0; pi < NCHILD; pi++){ // 循环创建NCHILD个子进程。
    fname = names[pi]; // 获取当前进程对应的文件名。
    unlink(fname); // 删除可能存在的文件。

    pid = fork(); // fork一个子进程。
    if(pid < 0){ // 如果fork失败。
      printf("%s: fork failed\n", s); // 打印错误信息。
      exit(1); // 退出并报告失败。
    }

    if(pid == 0){ // 子进程。
      fd = open(fname, O_CREATE | O_RDWR); // 创建并以读写模式打开文件。
      if(fd < 0){ // 如果打开失败。
        printf("%s: create failed\n", s); // 打印错误信息。
        exit(1); // 退出并报告失败。
      }

      memset(buf, '0'+pi, SZ); // 用'0'+pi填充缓冲区。
      for(i = 0; i < N; i++){ // 循环N次写入。
        if((n = write(fd, buf, SZ)) != SZ){ // 写入SZ字节数据。
          printf("write failed %d\n", n); // 打印错误信息。
          exit(1); // 退出并报告失败。
        }
      }
      exit(0); // 成功退出。
    }
  }

  int xstatus; // 退出状态。
  for(pi = 0; pi < NCHILD; pi++){ // 父进程等待所有子进程退出。
    wait(&xstatus); // 等待子进程退出。
    if(xstatus != 0) // 如果退出状态不为0。
      exit(xstatus); // 以子进程的退出状态退出。
  }

  for(i = 0; i < NCHILD; i++){ // 循环检查每个文件。
    fname = names[i]; // 获取文件名。
    fd = open(fname, 0); // 以只读模式打开文件。
    total = 0; // 初始化总读取字节数。
    while((n = read(fd, buf, sizeof(buf))) > 0){ // 循环读取文件内容。
      for(j = 0; j < n; j++){ // 遍历读取的字节。
        if(buf[j] != '0'+i){ // 验证读取的数据是否正确。
          printf("%s: wrong char\n", s); // 打印错误信息。
          exit(1); // 退出并报告失败。
        }
      }
      total += n; // 累加总读取字节数。
    }
    close(fd); // 关闭文件。
    if(total != N*SZ){ // 如果总读取字节数不等于预期值。
      printf("wrong length %d\n", total); // 打印错误信息。
      exit(1); // 退出并报告失败。
    }
    unlink(fname); // 删除文件。
  }
}

// 四个进程在同一个目录中创建和删除不同的文件
void
createdelete(char *s) // createdelete测试函数。
{
  enum { N = 20, NCHILD=4 }; // 定义每个进程创建N次文件，子进程数量NCHILD。
  int pid, i, fd, pi; // 进程ID，循环变量，文件描述符，进程索引。
  char name[32]; // 文件名缓冲区。

  for(pi = 0; pi < NCHILD; pi++){ // 循环创建NCHILD个子进程。
    pid = fork(); // fork一个子进程。
    if(pid < 0){ // 如果fork失败。
      printf("%s: fork failed\n", s); // 打印错误信息。
      exit(1); // 退出并报告失败。
    }

    if(pid == 0){ // 子进程。
      name[0] = 'p' + pi; // 文件名第一个字符为'p'+pi。
      name[2] = '\0'; // 文件名终止符。
      for(i = 0; i < N; i++){ // 循环N次。
        name[1] = '0' + i; // 文件名第二个字符。
        fd = open(name, O_CREATE | O_RDWR); // 创建并打开文件。
        if(fd < 0){ // 如果打开失败。
          printf("%s: create failed\n", s); // 打印错误信息。
          exit(1); // 退出并报告失败。
        }
        close(fd); // 关闭文件。
        if(i > 0 && (i % 2 ) == 0){ // 如果i大于0且是偶数。
          name[1] = '0' + (i / 2); // 构造一个不同的文件名。
          if(unlink(name) < 0){ // 删除该文件。
            printf("%s: unlink failed\n", s); // 打印错误信息。
            exit(1); // 退出并报告失败。
          }
        }
      }
      exit(0); // 成功退出。
    }
  }

  int xstatus; // 退出状态。
  for(pi = 0; pi < NCHILD; pi++){ // 父进程等待所有子进程退出。
    wait(&xstatus); // 等待子进程退出。
    if(xstatus != 0) // 如果退出状态不为0。
      exit(1); // 退出并报告失败。
  }

  name[0] = name[1] = name[2] = 0; // 清空文件名缓冲区。
  for(i = 0; i < N; i++){ // 循环检查每个可能的文件的状态。
    for(pi = 0; pi < NCHILD; pi++){ // 循环每个子进程对应的文件名。
      name[0] = 'p' + pi; // 文件名第一个字符。
      name[1] = '0' + i; // 文件名第二个字符。
      fd = open(name, 0); // 尝试打开文件。
      if((i == 0 || i >= N/2) && fd < 0){ // 如果是第一次创建或未被删除的文件，应该存在。
        printf("%s: oops createdelete %s didn't exist\n", s, name); // 打印错误信息。
        exit(1); // 退出并报告失败。
      } else if((i >= 1 && i < N/2) && fd >= 0){ // 如果是应该被删除的文件，不应该存在。
        printf("%s: oops createdelete %s did exist\n", s, name); // 打印错误信息。
        exit(1); // 退出并报告失败。
      }
      if(fd >= 0) // 如果文件存在。
        close(fd); // 关闭文件。
    }
  }

  for(i = 0; i < N; i++){ // 清理所有文件。
    for(pi = 0; pi < NCHILD; pi++){
      name[0] = 'p' + pi;
      name[1] = '0' + i;
      unlink(name); // 删除文件。
    }
  }
}

// 我可以删除一个文件后仍然读取它吗？
void
unlinkread(char *s) // unlinkread测试函数。
{
  enum { SZ = 5 }; // 定义写入字节数。
  int fd, fd1; // 文件描述符。

  fd = open("unlinkread", O_CREATE | O_RDWR); // 创建并以读写模式打开unlinkread文件。
  if(fd < 0){ // 如果打开失败。
    printf("%s: create unlinkread failed\n", s); // 打印错误信息。
    exit(1); // 退出并报告失败。
  }
  write(fd, "hello", SZ); // 写入"hello"。
  close(fd); // 关闭文件。

  fd = open("unlinkread", O_RDWR); // 以读写模式再次打开unlinkread文件。
  if(fd < 0){ // 如果打开失败。
    printf("%s: open unlinkread failed\n", s); // 打印错误信息。
    exit(1); // 退出并报告失败。
  }
  if(unlink("unlinkread") != 0){ // 删除文件。
    printf("%s: unlink unlinkread failed\n", s); // 打印错误信息。
    exit(1); // 退出并报告失败。
  }

  fd1 = open("unlinkread", O_CREATE | O_RDWR); // 创建一个同名的新文件。
  write(fd1, "yyy", 3); // 写入"yyy"。
  close(fd1); // 关闭文件。

  if(read(fd, buf, sizeof(buf)) != SZ){ // 从旧的文件描述符fd读取数据。
    printf("%s: unlinkread read failed", s); // 打印错误信息。
    exit(1); // 退出并报告失败。
  }
  if(buf[0] != 'h'){ // 验证读取的内容是否正确（应该读取到"hello"）。
    printf("%s: unlinkread wrong data\n", s); // 打印错误信息。
    exit(1); // 退出并报告失败。
  }
  if(write(fd, buf, 10) != 10){ // 通过旧文件描述符fd写入数据。
    printf("%s: unlinkread write failed\n", s); // 打印错误信息。
    exit(1); // 退出并报告失败。
  }
  close(fd); // 关闭文件。
  unlink("unlinkread"); // 删除新文件。
}

void
linktest(char *s) // linktest测试函数，测试硬链接。
{
  enum { SZ = 5 }; // 定义写入字节数。
  int fd; // 文件描述符。

  unlink("lf1"); // 删除可能存在的lf1和lf2文件。
  unlink("lf2");

  fd = open("lf1", O_CREATE|O_RDWR); // 创建并以读写模式打开lf1文件。
  if(fd < 0){ // 如果打开失败。
    printf("%s: create lf1 failed\n", s); // 打印错误信息。
    exit(1); // 退出并报告失败。
  }
  if(write(fd, "hello", SZ) != SZ){ // 写入"hello"。
    printf("%s: write lf1 failed\n", s); // 打印错误信息。
    exit(1); // 退出并报告失败。
  }
  close(fd); // 关闭文件。

  if(link("lf1", "lf2") < 0){ // 创建lf1到lf2的硬链接。
    printf("%s: link lf1 lf2 failed\n", s); // 打印错误信息。
    exit(1); // 退出并报告失败。
  }
  unlink("lf1"); // 删除lf1。此时文件内容仍可通过lf2访问。

  if(open("lf1", 0) >= 0){ // 尝试打开lf1（应该失败）。
    printf("%s: unlinked lf1 but it is still there!\n", s); // 打印错误信息。
    exit(1); // 退出并报告失败。
  }

  fd = open("lf2", 0); // 打开lf2。
  if(fd < 0){ // 如果打开失败。
    printf("%s: open lf2 failed\n", s); // 打印错误信息。
    exit(1); // 退出并报告失败。
  }
  if(read(fd, buf, sizeof(buf)) != SZ){ // 读取lf2内容。
    printf("%s: read lf2 failed\n", s); // 打印错误信息。
    exit(1); // 退出并报告失败。
  }
  close(fd); // 关闭文件。

  if(link("lf2", "lf2") >= 0){ // 尝试将lf2链接到自身（应该失败）。
    printf("%s: link lf2 lf2 succeeded! oops\n", s); // 打印错误信息。
    exit(1); // 退出并报告失败。
  }

  unlink("lf2"); // 删除lf2。
  if(link("lf2", "lf1") >= 0){ // 尝试将不存在的lf2链接到lf1（应该失败）。
    printf("%s: link non-existent succeeded! oops\n", s); // 打印错误信息。
    exit(1); // 退出并报告失败。
  }

  if(link(".", "lf1") >= 0){ // 尝试将当前目录链接到lf1（应该失败）。
    printf("%s: link . lf1 succeeded! oops\n", s); // 打印错误信息。
    exit(1); // 退出并报告失败。
  }
}

// test concurrent create/link/unlink of the same file
// 测试同一文件的并发创建/链接/删除
void
concreate(char *s) // concreate测试函数。
{
  enum { N = 40 }; // 定义循环次数N。
  char file[3]; // 文件名缓冲区。
  int i, pid, n, fd; // 循环变量，进程ID，读取字节数，文件描述符。
  char fa[N]; // 标记文件是否存在的数组。
  struct { // 目录项结构。
    ushort inum; // inode号。
    char name[DIRSIZ]; // 文件名。
  } de;

  file[0] = 'C'; // 文件名第一个字符为'C'。
  file[2] = '\0'; // 文件名终止符。
  for(i = 0; i < N; i++){ // 循环N次。
    file[1] = '0' + i; // 文件名第二个字符。
    unlink(file); // 删除可能存在的文件。
    pid = fork(); // fork一个子进程。
    if(pid && (i % 3) == 1){ // 父进程且i%3==1。
      link("C0", file); // 链接C0到当前文件。
    } else if(pid == 0 && (i % 5) == 1){ // 子进程且i%5==1。
      link("C0", file); // 链接C0到当前文件。
    } else { // 其他情况。
      fd = open(file, O_CREATE | O_RDWR); // 创建并打开文件。
      if(fd < 0){ // 如果打开失败。
        printf("concreate create %s failed\n", file); // 打印错误信息。
        exit(1); // 退出并报告失败。
      }
      close(fd); // 关闭文件。
    }
    if(pid == 0) { // 子进程。
      exit(0); // 成功退出。
    } else { // 父进程。
      int xstatus; // 退出状态。
      wait(&xstatus); // 等待子进程退出。
      if(xstatus != 0) // 如果退出状态不为0。
        exit(1); // 退出并报告失败。
    }
  }

  memset(fa, 0, sizeof(fa)); // 清空fa数组。
  fd = open(".", 0); // 打开当前目录。
  n = 0; // 初始化文件计数。
  while(read(fd, &de, sizeof(de)) > 0){ // 循环读取目录项。
    if(de.inum == 0) // 如果inode号为0，跳过。
      continue;
    if(de.name[0] == 'C' && de.name[2] == '\0'){ // 如果是'C'开头的文件。
      i = de.name[1] - '0'; // 获取数字部分。
      if(i < 0 || i >= sizeof(fa)){ // 检查索引是否有效。
        printf("%s: concreate weird file %s\n", s, de.name); // 打印错误信息。
        exit(1); // 退出并报告失败。
      }
      if(fa[i]){ // 如果该文件已经存在。
        printf("%s: concreate duplicate file %s\n", s, de.name); // 打印错误信息。
        exit(1); // 退出并报告失败。
      }
      fa[i] = 1; // 标记文件已存在。
      n++; // 增加文件计数。
    }
  }
  close(fd); // 关闭目录。

  if(n != N){ // 如果文件计数不等于N。
    printf("%s: concreate not enough files in directory listing\n", s); // 打印错误信息。
    exit(1); // 退出并报告失败。
  }

  for(i = 0; i < N; i++){ // 循环N次进行删除/打开操作。
    file[1] = '0' + i; // 文件名。
    pid = fork(); // fork一个子进程。
    if(pid < 0){ // 如果fork失败。
      printf("%s: fork failed\n", s); // 打印错误信息。
      exit(1); // 退出并报告失败。
    }
    if(((i % 3) == 0 && pid == 0) || // 如果子进程且i%3==0，或者父进程且i%3==1。
       ((i % 3) == 1 && pid != 0)){
      close(open(file, 0)); // 打开并关闭文件多次。
      close(open(file, 0));
      close(open(file, 0));
      close(open(file, 0));
      close(open(file, 0));
      close(open(file, 0));
    } else { // 其他情况。
      unlink(file); // 删除文件多次。
      unlink(file);
      unlink(file);
      unlink(file);
      unlink(file);
      unlink(file);
    }
    if(pid == 0) // 子进程。
      exit(0); // 成功退出。
    else // 父进程。
      wait(0); // 等待子进程退出。
  }
}

// 另一个并发链接/删除/创建测试，
// 用于查找死锁。
void
linkunlink(char *s) // linkunlink测试函数。
{
  int pid, i; // 进程ID，循环变量。

  unlink("x"); // 删除可能存在的x文件。
  pid = fork(); // fork一个子进程。
  if(pid < 0){ // 如果fork失败。
    printf("%s: fork failed\n", s); // 打印错误信息。
    exit(1); // 退出并报告失败。
  }

  unsigned int x = (pid ? 1 : 97); // 根据进程ID初始化随机数种子。
  for(i = 0; i < 100; i++){ // 循环100次。
    x = x * 1103515245 + 12345; // 更新随机数。
    if((x % 3) == 0){ // 随机选择操作：创建文件。
      close(open("x", O_RDWR | O_CREATE));
    } else if((x % 3) == 1){ // 随机选择操作：链接文件。
      link("cat", "x");
    } else { // 随机选择操作：删除文件。
      unlink("x");
    }
  }

  if(pid) // 父进程。
    wait(0); // 等待子进程退出。
  else // 子进程。
    exit(0); // 成功退出。
}


void
subdir(char *s) // subdir测试函数，测试子目录和路径解析。
{
  int fd, cc; // 文件描述符，读取字节数。

  unlink("ff"); // 删除可能存在的ff文件。
  if(mkdir("dd") != 0){ // 创建目录dd。
    printf("%s: mkdir dd failed\n", s); // 打印错误信息。
    exit(1); // 退出并报告失败。
  }

  fd = open("dd/ff", O_CREATE | O_RDWR); // 在dd目录下创建ff文件。
  if(fd < 0){ // 如果打开失败。
    printf("%s: create dd/ff failed\n", s); // 打印错误信息。
    exit(1); // 退出并报告失败。
  }
  write(fd, "ff", 2); // 写入"ff"。
  close(fd); // 关闭文件。

  if(unlink("dd") >= 0){ // 尝试删除非空目录dd（应该失败）。
    printf("%s: unlink dd (non-empty dir) succeeded!\n", s); // 打印错误信息。
    exit(1); // 退出并报告失败。
  }

  if(mkdir("/dd/dd") != 0){ // 在dd目录下创建dd目录。
    printf("%s: subdir mkdir dd/dd failed\n", s); // 打印错误信息。
    exit(1); // 退出并报告失败。
  }

  fd = open("dd/dd/ff", O_CREATE | O_RDWR); // 在dd/dd目录下创建ff文件。
  if(fd < 0){ // 如果打开失败。
    printf("%s: create dd/dd/ff failed\n", s); // 打印错误信息。
    exit(1); // 退出并报告失败。
  }
  write(fd, "FF", 2); // 写入"FF"。
  close(fd); // 关闭文件。

  fd = open("dd/dd/../ff", 0); // 打开dd/dd/../ff，路径解析后应该是dd/ff。
  if(fd < 0){ // 如果打开失败。
    printf("%s: open dd/dd/../ff failed\n", s); // 打印错误信息。
    exit(1); // 退出并报告失败。
  }
  cc = read(fd, buf, sizeof(buf)); // 读取内容。
  if(cc != 2 || buf[0] != 'f'){ // 验证内容是否为"ff"。
    printf("%s: dd/dd/../ff wrong content\n", s); // 打印错误信息。
    exit(1); // 退出并报告失败。
  }
  close(fd); // 关闭文件。

  if(link("dd/dd/ff", "dd/dd/ffff") != 0){ // 链接dd/dd/ff到dd/dd/ffff。
    printf("%s: link dd/dd/ff dd/dd/ffff failed\n", s); // 打印错误信息。
    exit(1); // 退出并报告失败。
  }

  if(unlink("dd/dd/ff") != 0){ // 删除dd/dd/ff。
    printf("%s: unlink dd/dd/ff failed\n", s); // 打印错误信息。
    exit(1); // 退出并报告失败。
  }
  if(open("dd/dd/ff", O_RDONLY) >= 0){ // 尝试打开已删除的dd/dd/ff（应该失败）。
    printf("%s: open (unlinked) dd/dd/ff succeeded\n", s); // 打印错误信息。
    exit(1); // 退出并报告失败。
  }

  if(chdir("dd") != 0){ // 改变当前目录到dd。
    printf("%s: chdir dd failed\n", s); // 打印错误信息。
    exit(1); // 退出并报告失败。
  }
  if(chdir("dd/../../dd") != 0){ // 改变目录，测试相对路径解析。
    printf("%s: chdir dd/../../dd failed\n", s); // 打印错误信息。
    exit(1); // 退出并报告失败。
  }
  if(chdir("dd/../../../dd") != 0){ // 改变目录，测试更多相对路径解析。
    printf("%s: chdir dd/../../../dd failed\n", s); // 打印错误信息。
    exit(1); // 退出并报告失败。
  }
  if(chdir("./..") != 0){ // 改变目录，测试./..。
    printf("%s: chdir ./.. failed\n", s); // 打印错误信息。
    exit(1); // 退出并报告失败。
  }

  fd = open("dd/dd/ffff", 0); // 打开dd/dd/ffff。
  if(fd < 0){ // 如果打开失败。
    printf("%s: open dd/dd/ffff failed\n", s); // 打印错误信息。
    exit(1); // 退出并报告失败。
  }
  if(read(fd, buf, sizeof(buf)) != 2){ // 读取内容。
    printf("%s: read dd/dd/ffff wrong len\n", s); // 打印错误信息。
    exit(1); // 退出并报告失败。
  }
  close(fd); // 关闭文件。

  if(open("dd/dd/ff", O_RDONLY) >= 0){ // 再次尝试打开已删除的dd/dd/ff（应该失败）。
    printf("%s: open (unlinked) dd/dd/ff succeeded!\n", s); // 打印错误信息。
    exit(1); // 退出并报告失败。
  }

  // 以下一系列测试尝试对文件/目录进行非法操作，都应失败。
  if(open("dd/ff/ff", O_CREATE|O_RDWR) >= 0){
    printf("%s: create dd/ff/ff succeeded!\n", s);
    exit(1);
  }
  if(open("dd/xx/ff", O_CREATE|O_RDWR) >= 0){
    printf("%s: create dd/xx/ff succeeded!\n", s);
    exit(1);
  }
  if(open("dd", O_CREATE) >= 0){
    printf("%s: create dd succeeded!\n", s);
    exit(1);
  }
  if(open("dd", O_RDWR) >= 0){
    printf("%s: open dd rdwr succeeded!\n", s);
    exit(1);
  }
  if(open("dd", O_WRONLY) >= 0){
    printf("%s: open dd wronly succeeded!\n", s);
    exit(1);
  }
  if(link("dd/ff/ff", "dd/dd/xx") == 0){
    printf("%s: link dd/ff/ff dd/dd/xx succeeded!\n", s);
    exit(1);
  }
  if(link("dd/xx/ff", "dd/dd/xx") == 0){
    printf("%s: link dd/xx/ff dd/dd/xx succeeded!\n", s);
    exit(1);
  }
  if(link("dd/ff", "dd/dd/ffff") == 0){
    printf("%s: link dd/ff dd/dd/ffff succeeded!\n", s);
    exit(1);
  }
  if(mkdir("dd/ff/ff") == 0){
    printf("%s: mkdir dd/ff/ff succeeded!\n", s);
    exit(1);
  }
  if(mkdir("dd/xx/ff") == 0){
    printf("%s: mkdir dd/xx/ff succeeded!\n", s);
    exit(1);
  }
  if(mkdir("dd/dd/ffff") == 0){
    printf("%s: mkdir dd/dd/ffff succeeded!\n", s);
    exit(1);
  }
  if(unlink("dd/xx/ff") == 0){
    printf("%s: unlink dd/xx/ff succeeded!\n", s);
    exit(1);
  }
  if(unlink("dd/ff/ff") == 0){
    printf("%s: unlink dd/ff/ff succeeded!\n", s);
    exit(1);
  }
  if(chdir("dd/ff") == 0){
    printf("%s: chdir dd/ff succeeded!\n", s);
    exit(1);
  }
  if(chdir("dd/xx") == 0){
    printf("%s: chdir dd/xx succeeded!\n", s);
    exit(1);
  }

  if(unlink("dd/dd/ffff") != 0){ // 删除dd/dd/ffff。
    printf("%s: unlink dd/dd/ff failed\n", s); // 打印错误信息。
    exit(1); // 退出并报告失败。
  }
  if(unlink("dd/ff") != 0){ // 删除dd/ff。
    printf("%s: unlink dd/ff failed\n", s); // 打印错误信息。
    exit(1); // 退出并报告失败。
  }
  if(unlink("dd") == 0){ // 尝试删除非空目录dd（应该失败）。
    printf("%s: unlink non-empty dd succeeded!\n", s); // 打印错误信息。
    exit(1); // 退出并报告失败。
  }
  if(unlink("dd/dd") < 0){ // 删除dd/dd。
    printf("%s: unlink dd/dd failed\n", s); // 打印错误信息。
    exit(1); // 退出并报告失败。
  }
  if(unlink("dd") < 0){ // 删除dd。
    printf("%s: unlink dd failed\n", s); // 打印错误信息。
    exit(1); // 退出并报告失败。
  }
}

// test writes that are larger than the log.
// 测试大于日志大小的写入。
void
bigwrite(char *s) // bigwrite测试函数。
{
  int fd, sz; // 文件描述符，写入大小。

  unlink("bigwrite"); // 删除可能存在的bigwrite文件。
  for(sz = 499; sz < (MAXOPBLOCKS+2)*BSIZE; sz += 471){ // 循环不同的写入大小。
    fd = open("bigwrite", O_CREATE | O_RDWR); // 创建并以读写模式打开bigwrite文件。
    if(fd < 0){ // 如果打开失败。
      printf("%s: cannot create bigwrite\n", s); // 打印错误信息。
      exit(1); // 退出并报告失败。
    }
    int i; // 循环变量。
    for(i = 0; i < 2; i++){ // 写入两次。
      int cc = write(fd, buf, sz); // 写入sz字节数据。
      if(cc != sz){ // 如果写入的字节数不等于sz。
        printf("%s: write(%d) ret %d\n", s, sz, cc); // 打印错误信息。
        exit(1); // 退出并报告失败。
      }
    }
    close(fd); // 关闭文件。
    unlink("bigwrite"); // 删除文件。
  }
}


void
bigfile(char *s) // bigfile测试函数，测试大文件读写。
{
  enum { N = 20, SZ=600 }; // 定义写入N次，每次SZ字节。
  int fd, i, total, cc; // 文件描述符，循环变量，总读取字节数，当前读取字节数。

  unlink("bigfile.dat"); // 删除可能存在的bigfile.dat文件。
  fd = open("bigfile.dat", O_CREATE | O_RDWR); // 创建并以读写模式打开bigfile.dat文件。
  if(fd < 0){ // 如果打开失败。
    printf("%s: cannot create bigfile", s); // 打印错误信息。
    exit(1); // 退出并报告失败。
  }
  for(i = 0; i < N; i++){ // 循环N次写入。
    memset(buf, i, SZ); // 用i填充缓冲区。
    if(write(fd, buf, SZ) != SZ){ // 写入SZ字节数据。
      printf("%s: write bigfile failed\n", s); // 打印错误信息。
      exit(1); // 退出并报告失败。
    }
  }
  close(fd); // 关闭文件。

  fd = open("bigfile.dat", 0); // 以只读模式打开bigfile.dat文件。
  if(fd < 0){ // 如果打开失败。
    printf("%s: cannot open bigfile\n", s); // 打印错误信息。
    exit(1); // 退出并报告失败。
  }
  total = 0; // 初始化总读取字节数。
  for(i = 0; ; i++){ // 无限循环读取。
    cc = read(fd, buf, SZ/2); // 读取SZ/2字节数据。
    if(cc < 0){ // 如果读取失败。
      printf("%s: read bigfile failed\n", s); // 打印错误信息。
      exit(1); // 退出并报告失败。
    }
    if(cc == 0) // 如果读取到文件末尾。
      break; // 跳出循环。
    if(cc != SZ/2){ // 如果读取的字节数不等于SZ/2。
      printf("%s: short read bigfile\n", s); // 打印错误信息。
      exit(1); // 退出并报告失败。
    }
    if(buf[0] != i/2 || buf[SZ/2-1] != i/2){ // 验证读取的数据是否正确。
      printf("%s: read bigfile wrong data\n", s); // 打印错误信息。
      exit(1); // 退出并报告失败。
    }
    total += cc; // 累加总读取字节数。
  }
  close(fd); // 关闭文件。
  if(total != N*SZ){ // 如果总读取字节数不等于预期值。
    printf("%s: read bigfile wrong total\n", s); // 打印错误信息。
    exit(1); // 退出并报告失败。
  }
  unlink("bigfile.dat"); // 删除文件。
}

void
fourteen(char *s) // fourteen测试函数，测试文件名长度限制 (DIRSIZ = 14)。
{
  int fd; // 文件描述符。

  // DIRSIZ is 14.
  // DIRSIZ是14。

  if(mkdir("12345678901234") != 0){ // 创建一个长度为14的目录名。
    printf("%s: mkdir 12345678901234 failed\n", s); // 打印错误信息。
    exit(1); // 退出并报告失败。
  }
  if(mkdir("12345678901234/123456789012345") != 0){ // 尝试在长度为14的目录名下创建长度为15的目录名（应该失败，xv6的DIRSIZ是14）。
    printf("%s: mkdir 12345678901234/123456789012345 failed\n", s); // 打印错误信息。
    exit(1); // 退出并报告失败。
  }
  fd = open("123456789012345/123456789012345/123456789012345", O_CREATE); // 尝试创建带有超长路径的文件名。
  if(fd < 0){ // 如果创建失败。
    printf("%s: create 123456789012345/123456789012345/123456789012345 failed\n", s); // 打印错误信息。
    exit(1); // 退出并报告失败。
  }
  close(fd); // 关闭文件。
  fd = open("12345678901234/12345678901234/12345678901234", 0); // 打开一个路径组件均为14个字符的文件。
  if(fd < 0){ // 如果打开失败。
    printf("%s: open 12345678901234/12345678901234/12345678901234 failed\n", s); // 打印错误信息。
    exit(1); // 退出并报告失败。
  }
  close(fd); // 关闭文件。

  if(mkdir("12345678901234/12345678901234") == 0){ // 尝试在长度为14的目录名下创建长度为14的目录名（应该成功）。
    printf("%s: mkdir 12345678901234/12345678901234 succeeded!\n", s); // 打印错误信息。
    exit(1); // 退出并报告失败。
  }
  if(mkdir("123456789012345/12345678901234") == 0){ // 尝试在超长目录名下创建目录（应该失败）。
    printf("%s: mkdir 12345678901234/123456789012345 succeeded!\n", s); // 打印错误信息。
    exit(1); // 退出并报告失败。
  }

  // 清理
  unlink("123456789012345/12345678901234");
  unlink("12345678901234/12345678901234");
  unlink("12345678901234/12345678901234/12345678901234");
  unlink("123456789012345/123456789012345/123456789012345");
  unlink("12345678901234/123456789012345");
  unlink("12345678901234");
}

void
rmdot(char *s) // rmdot测试函数，测试删除.和..。
{
  if(mkdir("dots") != 0){ // 创建目录dots。
    printf("%s: mkdir dots failed\n", s); // 打印错误信息。
    exit(1); // 退出并报告失败。
  }
  if(chdir("dots") != 0){ // 改变当前目录到dots。
    printf("%s: chdir dots failed\n", s); // 打印错误信息。
    exit(1); // 退出并报告失败。
  }
  if(unlink(".") == 0){ // 尝试删除当前目录.（应该失败）。
    printf("%s: rm . worked!\n", s); // 打印错误信息。
    exit(1); // 退出并报告失败。
  }
  if(unlink("..") == 0){ // 尝试删除父目录..（应该失败）。
    printf("%s: rm .. worked!\n", s); // 打印错误信息。
    exit(1); // 退出并报告失败。
  }
  if(chdir("/") != 0){ // 改变当前目录到根目录。
    printf("%s: chdir / failed\n", s); // 打印错误信息。
    exit(1); // 退出并报告失败。
  }
  if(unlink("dots/.") == 0){ // 尝试删除dots/.（应该失败）。
    printf("%s: unlink dots/. worked!\n", s); // 打印错误信息。
    exit(1); // 退出并报告失败。
  }
  if(unlink("dots/..") == 0){ // 尝试删除dots/..（应该失败）。
    printf("%s: unlink dots/.. worked!\n", s); // 打印错误信息。
    exit(1); // 退出并报告失败。
  }
  if(unlink("dots") != 0){ // 删除dots。
    printf("%s: unlink dots failed!\n", s); // 打印错误信息。
    exit(1); // 退出并报告失败。
  }
}

void
dirfile(char *s) // dirfile测试函数，测试目录与文件的冲突。
{
  int fd; // 文件描述符。

  fd = open("dirfile", O_CREATE); // 创建一个名为dirfile的文件。
  if(fd < 0){ // 如果创建失败。
    printf("%s: create dirfile failed\n", s); // 打印错误信息。
    exit(1); // 退出并报告失败。
  }
  close(fd); // 关闭文件。
  if(chdir("dirfile") == 0){ // 尝试改变目录到dirfile（应该失败，dirfile是文件）。
    printf("%s: chdir dirfile succeeded!\n", s); // 打印错误信息。
    exit(1); // 退出并报告失败。
  }
  fd = open("dirfile/xx", 0); // 尝试打开dirfile/xx（应该失败，dirfile不是目录）。
  if(fd >= 0){ // 如果成功。
    printf("%s: create dirfile/xx succeeded!\n", s); // 打印错误信息。
    exit(1); // 退出并报告失败。
  }
  fd = open("dirfile/xx", O_CREATE); // 尝试创建dirfile/xx（应该失败）。
  if(fd >= 0){ // 如果成功。
    printf("%s: create dirfile/xx succeeded!\n", s); // 打印错误信息。
    exit(1); // 退出并报告失败。
  }
  if(mkdir("dirfile/xx") == 0){ // 尝试创建目录dirfile/xx（应该失败）。
    printf("%s: mkdir dirfile/xx succeeded!\n", s); // 打印错误信息。
    exit(1); // 退出并报告失败。
  }
  if(unlink("dirfile/xx") == 0){ // 尝试删除dirfile/xx（应该失败）。
    printf("%s: unlink dirfile/xx succeeded!\n", s); // 打印错误信息。
    exit(1); // 退出并报告失败。
  }
  if(link("README", "dirfile/xx") == 0){ // 尝试链接README到dirfile/xx（应该失败）。
    printf("%s: link to dirfile/xx succeeded!\n", s); // 打印错误信息。
    exit(1); // 退出并报告失败。
  }
  if(unlink("dirfile") != 0){ // 删除dirfile文件。
    printf("%s: unlink dirfile failed!\n", s); // 打印错误信息。
    exit(1); // 退出并报告失败。
  }

  fd = open(".", O_RDWR); // 尝试以读写模式打开当前目录.（应该失败）。
  if(fd >= 0){ // 如果成功。
    printf("%s: open . for writing succeeded!\n", s); // 打印错误信息。
    exit(1); // 退出并报告失败。
  }
  fd = open(".", 0); // 以只读模式打开当前目录.。
  if(write(fd, "x", 1) > 0){ // 尝试向目录写入数据（应该失败）。
    printf("%s: write . succeeded!\n", s); // 打印错误信息。
    exit(1); // 退出并报告失败。
  }
  close(fd); // 关闭文件。
}

// 测试iput()在_namei()结束时被调用。
// 也测试空文件名。
void
iref(char *s) // iref测试函数。
{
  int i, fd; // 循环变量，文件描述符。

  for(i = 0; i < NINODE + 1; i++){ // 循环NINODE+1次。
    if(mkdir("irefd") != 0){ // 创建目录irefd。
      printf("%s: mkdir irefd failed\n", s); // 打印错误信息。
      exit(1); // 退出并报告失败。
    }
    if(chdir("irefd") != 0){ // 改变当前目录到irefd。
      printf("%s: chdir irefd failed\n", s); // 打印错误信息。
      exit(1); // 退出并报告失败。
    }

    mkdir(""); // 尝试创建空文件名目录（应该失败）。
    link("README", ""); // 尝试链接README到空文件名（应该失败）。
    fd = open("", O_CREATE); // 尝试创建空文件名文件（应该失败）。
    if(fd >= 0)
      close(fd);
    fd = open("xx", O_CREATE); // 创建xx文件。
    if(fd >= 0)
      close(fd);
    unlink("xx"); // 删除xx文件。
  }

  // clean up // 清理
  for(i = 0; i < NINODE + 1; i++){ // 循环清理。
    chdir(".."); // 改变目录到父目录。
    unlink("irefd"); // 删除irefd。
  }

  chdir("/"); // 改变目录到根目录。
}

// 测试fork优雅地失败
// forktest二进制文件也做这个，但它首先耗尽进程条目。
// 在更大的usertests二进制文件中，我们首先耗尽内存。
void
forktest(char *s) // forktest测试函数。
{
  enum{ N = 1000 }; // 定义尝试fork的次数。
  int n, pid; // 计数器，进程ID。

  for(n=0; n<N; n++){ // 循环尝试fork。
    pid = fork(); // fork一个子进程。
    if(pid < 0) // 如果fork失败。
      break; // 跳出循环。
    if(pid == 0) // 子进程。
      exit(0); // 成功退出。
  }

  if (n == 0) { // 如果一次fork都没有成功。
    printf("%s: no fork at all!\n", s); // 打印错误信息。
    exit(1); // 退出并报告失败。
  }

  if(n == N){ // 如果fork了N次都成功。
    printf("%s: fork claimed to work 1000 times!\n", s); // 打印错误信息。
    exit(1); // 退出并报告失败。
  }

  for(; n > 0; n--){ // 等待所有成功fork的子进程退出。
    if(wait(0) < 0){ // 如果等待失败。
      printf("%s: wait stopped early\n", s); // 打印错误信息。
      exit(1); // 退出并报告失败。
    }
  }

  if(wait(0) != -1){ // 再次等待，此时应该没有子进程，返回-1。
    printf("%s: wait got too many\n", s); // 打印错误信息。
    exit(1); // 退出并报告失败。
  }
}

void
sbrkbasic(char *s) // sbrkbasic测试函数，测试sbrk的基本功能。
{
  enum { TOOMUCH=1024*1024*1024}; // 定义一个非常大的值 (1GB)。
  int i, pid, xstatus; // 循环变量，进程ID，退出状态。
  char *c, *a, *b; // 字符指针。

  // does sbrk() return the expected failure value?
  // sbrk()是否返回预期的失败值？
  pid = fork(); // fork一个子进程。
  if(pid < 0){ // 如果fork失败。
    printf("fork failed in sbrkbasic\n"); // 打印错误信息。
    exit(1); // 退出并报告失败。
  }
  if(pid == 0){ // 子进程。
    a = sbrk(TOOMUCH); // 尝试分配TOOMUCH字节内存。
    if(a == (char*)SBRK_ERROR){ // 如果sbrk失败。
      // it's OK if this fails.
      // 如果失败，那是正常的。
      exit(0); // 成功退出。
    }

    for(b = a; b < a+TOOMUCH; b += PGSIZE){ // 尝试访问所有分配的页面。
      *b = 99; // 写入数据。
    }

    // 我们不应该到达这里！sbrk(TOOMUCH)要么应该失败，
    // 要么（如果启用了惰性分配）页面错误应该会杀死这个进程。
    exit(1); // 退出并报告失败。
  }

  wait(&xstatus); // 父进程等待子进程退出。
  if(xstatus == 1){ // 如果子进程以1退出。
    printf("%s: too much memory allocated!\n", s); // 打印错误信息。
    exit(1); // 退出并报告失败。
  }

  // can one sbrk() less than a page?
  // 可以sbrk()小于一个页的内存吗？
  a = sbrk(0); // 获取当前堆顶部地址。
  for(i = 0; i < 5000; i++){ // 循环5000次。
    b = sbrk(1); // 每次分配1个字节。
    if(b != a){ // 检查返回的地址是否是紧接着前一个地址。
      printf("%s: sbrk test failed %d %p %p\n", s, i, a, b); // 打印错误信息。
      exit(1); // 退出并报告失败。
    }
    *b = 1; // 写入数据。
    a = b + 1; // 更新a为下一个期望的地址。
  }
  pid = fork(); // fork一个子进程。
  if(pid < 0){ // 如果fork失败。
    printf("%s: sbrk test fork failed\n", s); // 打印错误信息。
    exit(1); // 退出并报告失败。
  }
  c = sbrk(1); // 父进程分配1字节。
  c = sbrk(1); // 父进程再次分配1字节。
  if(c != a + 1){ // 检查分配地址是否正确。
    printf("%s: sbrk test failed post-fork\n", s); // 打印错误信息。
    exit(1); // 退出并报告失败。
  }
  if(pid == 0) // 子进程。
    exit(0); // 成功退出。
  wait(&xstatus); // 父进程等待子进程退出。
  exit(xstatus); // 以子进程的退出状态退出。
}

void
sbrkmuch(char *s) // sbrkmuch测试函数，测试大量sbrk操作。
{
  enum { BIG=100*1024*1024 }; // 定义一个大值 (100MB)。
  char *c, *oldbrk, *a, *lastaddr, *p; // 字符指针。
  uint64 amt; // 分配量。

  oldbrk = sbrk(0); // 保存初始堆顶部地址。

  // 可以将地址空间扩展到很大的值吗？
  a = sbrk(0); // 获取当前堆顶部地址。
  amt = BIG - (uint64)a; // 计算需要扩展的字节数以达到BIG。
  p = sbrk(amt); // 扩展堆空间。
  if (p != a) { // 如果返回的地址不正确。
    printf("%s: sbrk test failed to grow big address space; enough phys mem?\n", s); // 打印错误信息。
    exit(1); // 退出并报告失败。
  }

  lastaddr = (char*) (BIG-1); // 指向BIG-1地址。
  *lastaddr = 99; // 写入数据，确保页面被实际分配。

  // 可以解除分配吗？
  a = sbrk(0); // 获取当前堆顶部地址。
  c = sbrk(-PGSIZE); // 缩减一个页大小的内存。
  if(c == (char*)SBRK_ERROR){ // 如果缩减失败。
    printf("%s: sbrk could not deallocate\n", s); // 打印错误信息。
    exit(1); // 退出并报告失败。
  }
  c = sbrk(0); // 获取新的堆顶部地址。
  if(c != a - PGSIZE){ // 检查地址是否正确。
    printf("%s: sbrk deallocation produced wrong address, a %p c %p\n", s, a, c); // 打印错误信息。
    exit(1); // 退出并报告失败。
  }

  // 可以重新分配那个页面吗？
  a = sbrk(0); // 获取当前堆顶部地址。
  c = sbrk(PGSIZE); // 重新分配一个页大小的内存。
  if(c != a || sbrk(0) != a + PGSIZE){ // 检查分配地址是否正确。
    printf("%s: sbrk re-allocation failed, a %p c %p\n", s, a, c); // 打印错误信息。
    exit(1); // 退出并报告失败。
  }
  if(*lastaddr == 99){ // 检查之前写入的数据是否还在（不应该在）。
    // should be zero
    // 应该是零
    printf("%s: sbrk de-allocation didn't really deallocate\n", s); // 打印错误信息。
    exit(1); // 退出并报告失败。
  }

  a = sbrk(0); // 获取当前堆顶部地址。
  c = sbrk(-(sbrk(0) - oldbrk)); // 将堆大小恢复到初始状态。
  if(c != a){ // 检查返回地址是否正确。
    printf("%s: sbrk downsize failed, a %p c %p\n", s, a, c); // 打印错误信息。
    exit(1); // 退出并报告失败。
  }
}

// 我们可以读取内核的内存吗？
void
kernmem(char *s) // kernmem测试函数。
{
  char *a; // 字符指针。
  int pid; // 进程ID。

  for(a = (char*)(KERNBASE); a < (char*) (KERNBASE+2000000); a += 50000){ // 遍历内核空间的某些地址。
    pid = fork(); // fork一个子进程。
    if(pid < 0){ // 如果fork失败。
      printf("%s: fork failed\n", s); // 打印错误信息。
      exit(1); // 退出并报告失败。
    }
    if(pid == 0){ // 子进程。
      printf("%s: oops could read %p = %x\n", s, a, *a); // 尝试读取内核内存，并打印。
      exit(1); // 退出并报告失败（因为不应该成功读取内核内存）。
    }
    int xstatus; // 退出状态。
    wait(&xstatus); // 父进程等待子进程退出。
    if(xstatus != -1)  // did kernel kill child? // 如果内核没有杀死子进程。
      exit(1); // 退出并报告失败。
  }
}

// 用户代码不应该能够写入MAXVA以上的地址。
void
MAXVAplus(char *s) // MAXVAplus测试函数。
{
  volatile uint64 a = MAXVA; // 从MAXVA开始。
  for( ; a != 0; a <<= 1){ // 循环移动地址，直到为0。
    int pid; // 进程ID。
    pid = fork(); // fork一个子进程。
    if(pid < 0){ // 如果fork失败。
      printf("%s: fork failed\n", s); // 打印错误信息。
      exit(1); // 退出并报告失败。
    }
    if(pid == 0){ // 子进程。
      *(char*)a = 99; // 尝试写入到该地址。
      printf("%s: oops wrote %p\n", s, (void*)a); // 打印错误信息（如果写入成功）。
      exit(1); // 退出并报告失败。
    }
    int xstatus; // 退出状态。
    wait(&xstatus); // 父进程等待子进程退出。
    if(xstatus != -1)  // did kernel kill child? // 如果内核没有杀死子进程。
      exit(1); // 退出并报告失败。
  }
}

// 如果我们使系统内存耗尽，它会清理上次失败的分配吗？
void
sbrkfail(char *s) // sbrkfail测试函数。
{
  enum { BIG=100*1024*1024 }; // 定义一个大值 (100MB)。
  int i, xstatus; // 循环变量，退出状态。
  int fds[2]; // 管道文件描述符。
  char scratch; // 临时变量。
  char *c, *a; // 字符指针。
  int pids[10]; // 进程ID数组。
  int pid; // 进程ID。
  int failed; // 标记是否有分配失败。

  failed = 0; // 初始化failed为0。
  if(pipe(fds) != 0){ // 创建管道。
    printf("%s: pipe() failed\n", s); // 打印错误信息。
    exit(1); // 退出并报告失败。
  }
  for(i = 0; i < sizeof(pids)/sizeof(pids[0]); i++){ // 循环创建多个子进程。
    if((pids[i] = fork()) == 0){ // 子进程。
      // allocate a lot of memory // 分配大量内存。
      if (sbrk(BIG - (uint64)sbrk(0)) ==  (char*)SBRK_ERROR) // 尝试分配大量内存直到BIG。
        write(fds[1], "0", 1); // 如果失败，写入"0"到管道。
      else
        write(fds[1], "1", 1); // 如果成功，写入"1"到管道。
      // sit around until killed // 挂起直到被杀死。
      for(;;) pause(1000); // 持续暂停。
    }
    if(pids[i] != -1) { // 如果fork成功。
      read(fds[0], &scratch, 1); // 从管道读取子进程的分配结果。
      if(scratch == '0') // 如果子进程的分配失败。
        failed = 1; // 标记failed为1。
    }
  }
  if(!failed) { // 如果没有分配失败。
    printf("%s: no allocation failed; allocate more?\n", s); // 打印信息。
  }

  // 如果那些失败的分配释放了它们确实分配的页面，
  // 我们将能够在这里分配内存。
  c = sbrk(PGSIZE); // 尝试分配一个页的内存。
  for(i = 0; i < sizeof(pids)/sizeof(pids[0]); i++){ // 循环杀死所有子进程。
    if(pids[i] == -1) // 如果fork失败了。
      continue;
    kill(pids[i]); // 杀死子进程。
    wait(0); // 等待子进程退出。
  }
  if(c == (char*)SBRK_ERROR){ // 如果分配失败（意味着内存泄露）。
    printf("%s: failed sbrk leaked memory\n", s); // 打印错误信息。
    exit(1); // 退出并报告失败。
  }

  // 测试使用上面分配的页面运行fork
  pid = fork(); // fork一个子进程。
  if(pid < 0){ // 如果fork失败。
    printf("%s: fork failed\n", s); // 打印错误信息。
    exit(1); // 退出并报告失败。
  }
  if(pid == 0){ // 子进程。
    // 分配大量内存。这应该会产生一个错误。
    a = sbrk(10*BIG); // 尝试分配10倍BIG的内存。
    if(a == (char*)SBRK_ERROR){ // 如果失败。
      exit(0); // 成功退出。
    }
    printf("%s: allocate a lot of memory succeeded %d\n", s, 10*BIG); // 打印错误信息。
    exit(1); // 退出并报告失败。
  }
  wait(&xstatus); // 父进程等待子进程退出。
  if(xstatus != 0) // 如果退出状态不为0。
    exit(1); // 退出并报告失败。
}


// 测试从/向已分配内存进行读/写
void
sbrkarg(char *s) // sbrkarg测试函数。
{
  char *a; // 字符指针。
  int fd, n; // 文件描述符，写入字节数。

  a = sbrk(PGSIZE); // 分配一个页大小的内存。
  fd = open("sbrk", O_CREATE|O_WRONLY); // 创建并以只写模式打开sbrk文件。
  unlink("sbrk"); // 删除sbrk文件。
  if(fd < 0)  { // 如果打开失败。
    printf("%s: open sbrk failed\n", s); // 打印错误信息。
    exit(1); // 退出并报告失败。
  }
  if ((n = write(fd, a, PGSIZE)) < 0) { // 尝试将分配的页面内容写入文件。
    printf("%s: write sbrk failed\n", s); // 打印错误信息。
    exit(1); // 退出并报告失败。
  }
  close(fd); // 关闭文件。

  // test writes to allocated memory
  // 测试向已分配内存进行写入
  a = sbrk(PGSIZE); // 再次分配一个页大小的内存。
  if(pipe((int *) a) != 0){ // 尝试将pipe的fds数组放在新分配的页面中（应该会失败，因为fds是int[]，不是char*）。
    printf("%s: pipe() failed\n", s); // 打印错误信息。
    exit(1); // 退出并报告失败。
  }
}

void
validatetest(char *s) // validatetest测试函数，测试参数验证。
{
  int hi; // 高地址。
  uint64 p; // 指针。

  hi = 1100*1024; // 定义一个高地址限制。
  for(p = 0; p <= (uint)hi; p += PGSIZE){ // 遍历从0到hi的每个页起始地址。
    // try to crash the kernel by passing in a bad string pointer
    // 尝试通过传入一个错误的字符串指针来使内核崩溃。
    if(link("nosuchfile", (char*)p) != -1){ // 尝试链接不存在的文件到这些地址作为文件名（应该失败）。
      printf("%s: link should not succeed\n", s); // 打印错误信息。
      exit(1); // 退出并报告失败。
    }
  }
}

// 未初始化数据是否开始时为零？
char uninit[10000]; // 未初始化全局数组。
void
bsstest(char *s) // bsstest测试函数。
{
  int i; // 循环变量。

  for(i = 0; i < sizeof(uninit); i++){ // 遍历数组。
    if(uninit[i] != '\0'){ // 检查每个字节是否为零。
      printf("%s: bss test failed\n", s); // 打印错误信息。
      exit(1); // 退出并报告失败。
    }
  }
}

// 如果参数大于一个页面，exec是否返回错误？
// 或者它是否写入堆栈下方并破坏指令/数据？
void
bigargtest(char *s) // bigargtest测试函数。
{
  int pid, fd, xstatus; // 进程ID，文件描述符，退出状态。

  unlink("bigarg-ok"); // 删除可能存在的bigarg-ok文件。
  pid = fork(); // fork一个子进程。
  if(pid == 0){ // 子进程。
    static char *args[MAXARG]; // 参数数组。
    int i; // 循环变量。
    char big[400]; // 大缓冲区。
    memset(big, ' ', sizeof(big)); // 用空格填充缓冲区。
    big[sizeof(big)-1] = '\0'; // 终止字符串。
    for(i = 0; i < MAXARG-1; i++) // 填充args数组。
      args[i] = big; // 每个参数都是同一个大字符串。
    args[MAXARG-1] = 0; // 参数列表终止符。
    // this exec() should fail (and return) because the
    // arguments are too large.
    // 这个exec()应该失败（并返回），因为参数太大。
    exec("echo", args); // 执行echo程序。
    fd = open("bigarg-ok", O_CREATE); // 如果exec返回，则创建bigarg-ok文件。
    close(fd); // 关闭文件。
    exit(0); // 成功退出。
  } else if(pid < 0){ // 如果fork失败。
    printf("%s: bigargtest: fork failed\n", s); // 打印错误信息。
    exit(1); // 退出并报告失败。
  }

  wait(&xstatus); // 父进程等待子进程退出。
  if(xstatus != 0) // 如果退出状态不为0。
    exit(xstatus); // 以子进程的退出状态退出。
  fd = open("bigarg-ok", 0); // 打开bigarg-ok文件。
  if(fd < 0){ // 如果打开失败。
    printf("%s: bigarg test failed!\n", s); // 打印错误信息。
    exit(1); // 退出并报告失败。
  }
  close(fd); // 关闭文件。
}

// 当文件系统耗尽块时会发生什么？
// 答案：balloc会发生恐慌，所以这个测试不实用。
void
fsfull() // fsfull测试函数，测试文件系统耗尽块的情况。
{
  int nfiles; // 文件数量。
  int fsblocks = 0; // 文件系统块计数。

  printf("fsfull test\n"); // 打印信息。

  for(nfiles = 0; ; nfiles++){ // 循环创建文件。
    char name[64]; // 文件名缓冲区。
    name[0] = 'f'; // 文件名第一个字符为'f'。
    name[1] = '0' + nfiles / 1000; // 构造文件名。
    name[2] = '0' + (nfiles % 1000) / 100;
    name[3] = '0' + (nfiles % 100) / 10;
    name[4] = '0' + (nfiles % 10);
    name[5] = '\0';
    printf("writing %s\n", name); // 打印当前写入的文件名。
    int fd = open(name, O_CREATE|O_RDWR); // 创建并以读写模式打开文件。
    if(fd < 0){ // 如果打开失败。
      printf("open %s failed\n", name); // 打印错误信息。
      break; // 跳出循环。
    }
    int total = 0; // 总写入字节数。
    while(1){ // 循环写入数据。
      int cc = write(fd, buf, BSIZE); // 写入一个块的数据。
      if(cc < BSIZE) // 如果写入的字节数小于块大小，表示磁盘空间不足。
        break; // 跳出循环。
      total += cc; // 累加总写入字节数。
      fsblocks++; // 增加文件系统块计数。
    }
    printf("wrote %d bytes\n", total); // 打印写入的字节数。
    close(fd); // 关闭文件。
    if(total == 0) // 如果没有写入任何字节。
      break; // 跳出循环。
  }

  while(nfiles >= 0){ // 循环删除所有创建的文件。
    char name[64]; // 文件名缓冲区。
    name[0] = 'f'; // 构造文件名。
    name[1] = '0' + nfiles / 1000;
    name[2] = '0' + (nfiles % 1000) / 100;
    name[3] = '0' + (nfiles % 100) / 10;
    name[4] = '0' + (nfiles % 10);
    name[5] = '\0';
    unlink(name); // 删除文件。
    nfiles--; // 减少文件计数。
  }

  printf("fsfull test finished\n"); // 打印信息。
}

void argptest(char *s) // argptest测试函数，测试参数指针的验证。
{
  int fd; // 文件描述符。
  fd = open("init", O_RDONLY); // 打开init文件。
  if (fd < 0) { // 如果打开失败。
    printf("%s: open failed\n", s); // 打印错误信息。
    exit(1); // 退出并报告失败。
  }
  read(fd, sbrk(0) - 1, -1); // 尝试读取文件，使用sbrk(0)-1作为缓冲区地址，-1作为长度（非法）。
  close(fd); // 关闭文件。
}

// 检查用户栈下方是否存在一个无效页面，
// 以捕获栈溢出。
void
stacktest(char *s) // stacktest测试函数。
{
  int pid; // 进程ID。
  int xstatus; // 退出状态。

  pid = fork(); // fork一个子进程。
  if(pid == 0) { // 子进程。
    char *sp = (char *) r_sp(); // 获取栈指针。
    sp -= USERSTACK*PGSIZE; // 将栈指针向下移动到用户栈的底部下方。
    // the *sp should cause a trap.
    // *sp应该会导致一个陷阱。
    printf("%s: stacktest: read below stack %d\n", s, *sp); // 尝试读取该地址的内容。
    exit(1); // 退出并报告失败。
  } else if(pid < 0){ // 如果fork失败。
    printf("%s: fork failed\n", s); // 打印错误信息。
    exit(1); // 退出并报告失败。
  }
  wait(&xstatus); // 父进程等待子进程退出。
  if(xstatus == -1)  // kernel killed child? // 如果内核杀死了子进程。
    exit(0); // 成功退出。
  else
    exit(xstatus); // 以子进程的退出状态退出。
}

// 检查写入几个禁止的地址是否会导致故障，
// 例如进程的代码段和TRAMPOLINE。
void
nowrite(char *s) // nowrite测试函数。
{
  int pid; // 进程ID。
  int xstatus; // 退出状态。
  uint64 addrs[] = { 0, 0x80000000LL, 0x3fffffe000, 0x3ffffff000, 0x4000000000,
                     0xffffffffffffffff }; // 定义一系列禁止写入的地址。

  for(int ai = 0; ai < sizeof(addrs)/sizeof(addrs[0]); ai++){ // 遍历所有这些地址。
    pid = fork(); // fork一个子进程。
    if(pid == 0) { // 子进程。
      volatile int *addr = (int *) addrs[ai]; // 获取地址。
      *addr = 10; // 尝试写入数据。
      printf("%s: write to %p did not fail!\n", s, addr); // 打印错误信息（如果写入成功）。
      exit(0); // 退出并报告成功。
    } else if(pid < 0){ // 如果fork失败。
      printf("%s: fork failed\n", s); // 打印错误信息。
      exit(1); // 退出并报告失败。
    }
    wait(&xstatus); // 父进程等待子进程退出。
    if(xstatus == 0){ // kernel did not kill child! // 如果内核没有杀死子进程。
      exit(1); // 退出并报告失败。
    }
  }
  exit(0); // 成功退出。
}

// 回归测试。copyin()、copyout()和copyinstr()曾经将虚拟页面地址转换为uint，
// 这（在某些野生的系统调用参数下）会导致内核页面错误。
void *big = (void*) 0xeaeb0b5b00002f5e; // 一个非常大的地址。
void
pgbug(char *s) // pgbug测试函数。
{
  char *argv[1]; // 参数数组。
  argv[0] = 0; // 参数列表终止符。
  exec(big, argv); // 尝试执行该地址处的程序。
  pipe(big); // 尝试将pipe的fds数组放在该地址。

  exit(0); // 成功退出。
}

// 回归测试。如果一个进程的sbrk()将其大小减小到小于一个页面、或为零、
// 或将堆断点减小到不足以释放一个页面的程度，内核是否会发生恐慌？
void
sbrkbugs(char *s) // sbrkbugs测试函数。
{
  int pid = fork(); // fork一个子进程。
  if(pid < 0){ // 如果fork失败。
    printf("fork failed\n"); // 打印错误信息。
    exit(1); // 退出并报告失败。
  }
  if(pid == 0){ // 子进程。
    int sz = (uint64) sbrk(0); // 获取当前堆大小。
    // 释放所有用户内存；以前有一个bug，在这种情况下不会正确调整p->sz，
    // 导致exit()发生恐慌。
    sbrk(-sz); // 释放所有内存。
    // user page fault here. // 这里可能会发生用户页面错误。
    exit(0); // 成功退出。
  }
  wait(0); // 父进程等待子进程退出。

  pid = fork(); // fork另一个子进程。
  if(pid < 0){ // 如果fork失败。
    printf("fork failed\n"); // 打印错误信息。
    exit(1); // 退出并报告失败。
  }
  if(pid == 0){ // 子进程。
    int sz = (uint64) sbrk(0); // 获取当前堆大小。
    // 将堆断点设置到第一个页的某个位置；以前有一个bug，会错误地释放第一个页面。
    sbrk(-(sz - 3500)); // 缩减堆大小。
    exit(0); // 成功退出。
  }
  wait(0); // 父进程等待子进程退出。

  pid = fork(); // fork另一个子进程。
  if(pid < 0){ // 如果fork失败。
    printf("fork failed\n"); // 打印错误信息。
    exit(1); // 退出并报告失败。
  }
  if(pid == 0){ // 子进程。
    // set the break in the middle of a page.
    // 将堆断点设置在页的中间。
    sbrk((10*PGSIZE + 2048) - (uint64)sbrk(0)); // 扩展堆到10个页加2048字节。

    // 稍微减少堆断点，但不足以释放一个页面。这曾经导致恐慌。
    sbrk(-10); // 缩减10字节。

    exit(0); // 成功退出。
  }
  wait(0); // 父进程等待子进程退出。

  exit(0); // 成功退出。
}

// 如果进程大小略大于页边界，然后缩小到略小于该页边界，
// 内核是否仍然可以从最后一个页中的地址进行copyin()？
void
sbrklast(char *s) // sbrklast测试函数。
{
  uint64 top = (uint64) sbrk(0); // 获取当前堆顶部地址。
  if((top % PGSIZE) != 0) // 如果不是页对齐。
    sbrk(PGSIZE - (top % PGSIZE)); // 调整使其页对齐。
  sbrk(PGSIZE); // 分配一个页。
  sbrk(10); // 再分配10字节。
  sbrk(-20); // 缩减20字节。
  top = (uint64) sbrk(0); // 获取新的堆顶部地址。
  char *p = (char *) (top - 64); // 指向最后一个页内的某个地址。
  p[0] = 'x'; // 写入数据。
  p[1] = '\0'; // 终止字符串。
  int fd = open(p, O_RDWR|O_CREATE); // 尝试用p作为文件名创建文件。
  write(fd, p, 1); // 写入数据。
  close(fd); // 关闭文件。
  fd = open(p, O_RDWR); // 再次打开文件。
  p[0] = '\0'; // 清空p[0]。
  read(fd, p, 1); // 读取文件内容到p。
  if(p[0] != 'x') // 验证读取的内容。
    exit(1); // 退出并报告失败。
}


// sbrk是否处理带负参数的int32有符号整数环绕？
void
sbrk8000(char *s) // sbrk8000测试函数。
{
  sbrk(0x80000004); // 分配一个超过int32正数范围的内存量。
  volatile char *top = sbrk(0); // 获取堆顶部地址。
  *(top-1) = *(top-1) + 1; // 尝试写入最后一个字节，以确保其被映射。
}



// 回归测试。测试如果exec()的其中一个参数无效，是否会泄露内存。
// 如果内核不恐慌，则测试通过。
void
badarg(char *s) // badarg测试函数。
{
  for(int i = 0; i < 50000; i++){ // 循环50000次。
    char *argv[2]; // 参数数组。
    argv[0] = (char*)0xffffffff; // 第一个参数是一个无效地址。
    argv[1] = 0; // 参数列表终止符。
    exec("echo", argv); // 尝试执行echo程序，传入无效参数。
  }

  exit(0); // 成功退出。
}

#define REGION_SZ (1024 * 1024 * 1024) // 定义一个大的区域大小 (1GB)。

// 每隔64个页面触摸一个页面，这在惰性分配下会导致一个页面被分配。
void
lazy_alloc(char *s) // lazy_alloc测试函数。
{
  char *i, *prev_end, *new_end; // 字符指针。

  prev_end = sbrklazy(REGION_SZ); // 惰性分配一个大的区域。
  if (prev_end == (char *) SBRK_ERROR) { // 如果分配失败。
    printf("sbrklazy() failed\n"); // 打印错误信息。
    exit(1); // 退出并报告失败。
  }
  new_end = prev_end + REGION_SZ; // 计算区域的结束地址。

  for (i = prev_end + PGSIZE; i < new_end; i += 64 * PGSIZE) // 每隔64个页触摸一个页。
    *(char **)i = i; // 写入该页的地址到该页的起始位置。

  for (i = prev_end + PGSIZE; i < new_end; i += 64 * PGSIZE) { // 再次遍历，验证写入的内容。
    if (*(char **)i != i) { // 如果读取的内容不正确。
      printf("failed to read value from memory\n"); // 打印错误信息。
      exit(1); // 退出并报告失败。
    }
  }

  exit(0); // 成功退出。
}

// 在区域中每隔64个页面触摸一个页面，这在惰性分配下会导致一个页面被分配。
// 检查释放区域是否会释放已分配的页面。
void
lazy_unmap(char *s) // lazy_unmap测试函数。
{
  int pid; // 进程ID。
  char *i, *prev_end, *new_end; // 字符指针。

  prev_end = sbrklazy(REGION_SZ); // 惰性分配一个大的区域。
  if (prev_end == (char*)SBRK_ERROR) { // 如果分配失败。
    printf("sbrklazy() failed\n"); // 打印错误信息。
    exit(1); // 退出并报告失败。
  }
  new_end = prev_end + REGION_SZ; // 计算区域的结束地址。

  for (i = prev_end + PGSIZE; i < new_end; i += PGSIZE * PGSIZE) // 触摸某些页面。
    *(char **)i = i; // 写入该页的地址。

  for (i = prev_end + PGSIZE; i < new_end; i += PGSIZE * PGSIZE) { // 遍历已触摸的页面。
    pid = fork(); // fork一个子进程。
    if (pid < 0) { // 如果fork失败。
      printf("error forking\n"); // 打印错误信息。
      exit(1); // 退出并报告失败。
    } else if (pid == 0) { // 子进程。
      sbrklazy(-1L * REGION_SZ); // 释放整个惰性分配区域。
      *(char **)i = i; // 尝试访问已释放的页面（应该导致页面错误）。
      exit(0); // 退出并报告成功（如果页面错误被内核处理）。
    } else { // 父进程。
      int status; // 退出状态。
      wait(&status); // 等待子进程退出。
      if (status == 0) { // 如果子进程成功退出（意味着没有发生页面错误）。
        printf("memory not unmapped\n"); // 打印错误信息。
        exit(1); // 退出并报告失败。
      }
    }
  }

  exit(0); // 成功退出。
}

void
lazy_copy(char *s) // lazy_copy测试函数，测试copyin/copyout在惰性分配页面上的行为。
{
  // copyinstr on lazy page
  { // 代码块。
    char *p = sbrk(0); // 获取当前堆顶部。
    sbrklazy(4*PGSIZE); // 惰性分配4个页。
    open(p + 8192, 0); // 尝试打开一个位于惰性分配区域的地址作为文件名。
  }

  { // 代码块。
    void *xx = sbrk(0); // 获取当前堆顶部。
    void *ret = sbrk(-(((uint64) xx)+1)); // 尝试释放超出当前堆范围的内存。
    if(ret != xx){ // 如果返回地址不等于预期。
      printf("sbrk(sbrk(0)+1) returned %p, not old sz\n", ret); // 打印错误信息。
      exit(1); // 退出并报告失败。
    }
  }


  // 对这些地址的read()和write()应该失败。
  unsigned long bad[] = { // 定义一系列无效地址。
    0x3fffffc000,
    0x3fffffd000,
    0x3fffffe000,
    0x3ffffff000,
    0x4000000000,
    0x8000000000,
  };
  for(int i = 0; i < sizeof(bad)/sizeof(bad[0]); i++){ // 遍历这些无效地址。
    int fd = open("README", 0); // 打开README文件。
    if(fd < 0) { printf("cannot open README\n"); exit(1); } // 如果打开失败。
    if(read(fd, (char*)bad[i], 512) >= 0) { printf("read succeeded\n");  exit(1); } // 尝试读取到无效地址（应该失败）。
    close(fd); // 关闭文件。
    fd = open("junk", O_CREATE|O_RDWR|O_TRUNC); // 创建并打开junk文件。
    if(fd < 0) { printf("cannot open junk\n"); exit(1); } // 如果打开失败。
    if(write(fd, (char*)bad[i], 512) >= 0) { printf("write succeeded\n"); exit(1); } // 尝试从无效地址写入（应该失败）。
    close(fd); // 关闭文件。
  }

  exit(0); // 成功退出。
}

// 定义一个结构体数组，包含所有“快速”测试的函数指针和名称。
struct test {
  void (*f)(char *); // 测试函数指针。
  char *s;           // 测试名称字符串。
} quicktests[] = {
  {copyin, "copyin"},
  {copyout, "copyout"},
  {copyinstr1, "copyinstr1"},
  {copyinstr2, "copyinstr2"},
  {copyinstr3, "copyinstr3"},
  {rwsbrk, "rwsbrk" },
  {truncate1, "truncate1"},
  {truncate2, "truncate2"},
  {truncate3, "truncate3"},
  {openiputtest, "openiput"},
  {exitiputtest, "exitiput"},
  {iputtest, "iput"},
  {opentest, "opentest"},
  {writetest, "writetest"},
  {writebig, "writebig"},
  {createtest, "createtest"},
  {dirtest, "dirtest"},
  {exectest, "exectest"},
  {pipe1, "pipe1"},
  {killstatus, "killstatus"},
  {preempt, "preempt"},
  {exitwait, "exitwait"},
  {reparent, "reparent" },
  {twochildren, "twochildren"},
  {forkfork, "forkfork"},
  {forkforkfork, "forkforkfork"},
  {reparent2, "reparent2"},
  {mem, "mem"},
  {sharedfd, "sharedfd"},
  {fourfiles, "fourfiles"},
  {createdelete, "createdelete"},
  {unlinkread, "unlinkread"},
  {linktest, "linktest"},
  {concreate, "concreate"},
  {linkunlink, "linkunlink"},
  {subdir, "subdir"},
  {bigwrite, "bigwrite"},
  {bigfile, "bigfile"},
  {fourteen, "fourteen"},
  {rmdot, "rmdot"},
  {dirfile, "dirfile"},
  {iref, "iref"},
  {forktest, "forktest"},
  {sbrkbasic, "sbrkbasic"},
  {sbrkmuch, "sbrkmuch"},
  {kernmem, "kernmem"},
  {MAXVAplus, "MAXVAplus"},
  {sbrkfail, "sbrkfail"},
  {sbrkarg, "sbrkarg"},
  {validatetest, "validatetest"},
  {bsstest, "bsstest"},
  {bigargtest, "bigargtest"},
  {argptest, "argptest"},
  {stacktest, "stacktest"},
  {nowrite, "nowrite"},
  {pgbug, "pgbug" },
  {sbrkbugs, "sbrkbugs" },
  {sbrklast, "sbrklast"},
  {sbrk8000, "sbrk8000"},
  {badarg, "badarg" },
  {lazy_alloc, "lazy_alloc"},
  {lazy_unmap, "lazy_unmap"},
  {lazy_copy, "lazy_copy"},
  { 0, 0}, // 数组结束标志。
};

//
// 耗时较长测试的章节
//

// 使用间接块的目录
void
bigdir(char *s) // bigdir测试函数，测试大目录。
{
  enum { N = 500 }; // 定义创建文件数量。
  int i, fd; // 循环变量，文件描述符。
  char name[10]; // 文件名缓冲区。

  unlink("bd"); // 删除可能存在的bd文件。

  fd = open("bd", O_CREATE); // 创建bd文件。
  if(fd < 0){ // 如果创建失败。
    printf("%s: bigdir create failed\n", s); // 打印错误信息。
    exit(1); // 退出并报告失败。
  }
  close(fd); // 关闭文件。

  for(i = 0; i < N; i++){ // 循环创建N个硬链接。
    name[0] = 'x'; // 文件名第一个字符为'x'。
    name[1] = '0' + (i / 64); // 构造文件名。
    name[2] = '0' + (i % 64);
    name[3] = '\0';
    if(link("bd", name) != 0){ // 链接bd到新文件。
      printf("%s: bigdir i=%d link(bd, %s) failed\n", s, i, name); // 打印错误信息。
      exit(1); // 退出并报告失败。
    }
  }

  unlink("bd"); // 删除原始文件bd。
  for(i = 0; i < N; i++){ // 循环删除所有硬链接。
    name[0] = 'x'; // 构造文件名。
    name[1] = '0' + (i / 64);
    name[2] = '0' + (i % 64);
    name[3] = '\0';
    if(unlink(name) != 0){ // 删除文件。
      printf("%s: bigdir unlink failed", s); // 打印错误信息。
      exit(1); // 退出并报告失败。
    }
  }
}

// 并发写入以尝试在virtio磁盘驱动程序中引发死锁。
void
manywrites(char *s) // manywrites测试函数。
{
  int nchildren = 4; // 子进程数量。
  int howmany = 30; // 写入次数（增加此值以查找死锁）。

  for(int ci = 0; ci < nchildren; ci++){ // 循环创建nchildren个子进程。
    int pid = fork(); // fork一个子进程。
    if(pid < 0){ // 如果fork失败。
      printf("fork failed\n"); // 打印错误信息。
      exit(1); // 退出并报告失败。
    }

    if(pid == 0){ // 子进程。
      char name[3]; // 文件名缓冲区。
      name[0] = 'b'; // 文件名第一个字符为'b'。
      name[1] = 'a' + ci; // 构造文件名。
      name[2] = '\0';
      unlink(name); // 删除可能存在的文件。

      for(int iters = 0; iters < howmany; iters++){ // 循环howmany次。
        for(int i = 0; i < ci+1; i++){ // 每个子进程写入不同次数。
          int fd = open(name, O_CREATE | O_RDWR); // 创建并以读写模式打开文件。
          if(fd < 0){ // 如果打开失败。
            printf("%s: cannot create %s\n", s, name); // 打印错误信息。
            exit(1); // 退出并报告失败。
          }
          int sz = sizeof(buf); // 缓冲区大小。
          int cc = write(fd, buf, sz); // 写入数据。
          if(cc != sz){ // 如果写入的字节数不等于sz。
            printf("%s: write(%d) ret %d\n", s, sz, cc); // 打印错误信息。
            exit(1); // 退出并报告失败。
          }
          close(fd); // 关闭文件。
        }
        unlink(name); // 删除文件。
      }

      unlink(name); // 再次删除文件。
      exit(0); // 成功退出。
    }
  }

  for(int ci = 0; ci < nchildren; ci++){ // 父进程等待所有子进程退出。
    int st = 0; // 退出状态。
    wait(&st); // 等待子进程退出。
    if(st != 0) // 如果退出状态不为0。
      exit(st); // 以子进程的退出状态退出。
  }
  exit(0); // 成功退出。
}

// 回归测试。如果write()使用无效的缓冲区指针，是否会导致为文件分配一个块，
// 然后在删除文件时未能释放该块？如果内核存在此错误，它将恐慌：balloc:
// out of blocks。assumed_free可能需要增加到大于空闲块的数量。此测试耗时较长。
void
badwrite(char *s) // badwrite测试函数。
{
  int assumed_free = 600; // 假设的空闲块数量。

  unlink("junk"); // 删除可能存在的junk文件。
  for(int i = 0; i < assumed_free; i++){ // 循环assumed_free次。
    int fd = open("junk", O_CREATE|O_WRONLY); // 创建并以只写模式打开junk文件。
    if(fd < 0){ // 如果打开失败。
      printf("open junk failed\n"); // 打印错误信息。
      exit(1); // 退出并报告失败。
    }
    write(fd, (char*)0xffffffffffL, 1); // 尝试从一个无效地址写入1个字节（应该失败）。
    close(fd); // 关闭文件。
    unlink("junk"); // 删除文件。
  }

  int fd = open("junk", O_CREATE|O_WRONLY); // 再次创建并打开junk文件。
  if(fd < 0){ // 如果打开失败。
    printf("open junk failed\n"); // 打印错误信息。
    exit(1); // 退出并报告失败。
  }
  if(write(fd, "x", 1) != 1){ // 写入一个有效的字节。
    printf("write failed\n"); // 打印错误信息。
    exit(1); // 退出并报告失败。
  }
  close(fd); // 关闭文件。
  unlink("junk"); // 删除文件。

  exit(0); // 成功退出。
}

// 测试exec()在内存不足时进行清理的代码。
// 这实际上是一个测试，即这种情况不会导致恐慌。
void
execout(char *s) // execout测试函数。
{
  for(int avail = 0; avail < 15; avail++){ // 循环不同的可用内存级别。
    int pid = fork(); // fork一个子进程。
    if(pid < 0){ // 如果fork失败。
      printf("fork failed\n"); // 打印错误信息。
      exit(1); // 退出并报告失败。
    } else if(pid == 0){ // 子进程。
      // allocate all of memory.
      // 分配所有内存。
      while(1){ // 循环分配内存，直到失败。
        char *a = sbrk(PGSIZE); // 分配一个页。
        if(a == SBRK_ERROR) // 如果分配失败。
          break; // 跳出循环。
        *(a + PGSIZE - 1) = 1; // 写入最后一个字节，确保页面被映射。
      }

      // free a few pages, in order to let exec() make some
      // progress.
      // 释放一些页面，以便exec()能够取得一些进展。
      for(int i = 0; i < avail; i++) // 根据avail释放一些页面。
        sbrk(-PGSIZE);

      close(1); // 关闭标准输出。
      char *args[] = { "echo", "x", 0 }; // 参数数组。
      exec("echo", args); // 执行echo程序。
      exit(0); // 成功退出。
    } else { // 父进程。
      wait((int*)0); // 等待子进程退出。
    }
  }

  exit(0); // 成功退出。
}

// 内核能否容忍磁盘空间不足？
void
diskfull(char *s) // diskfull测试函数。
{
  int fi; // 文件索引。
  int done = 0; // 完成标志。

  unlink("diskfulldir"); // 删除可能存在的diskfulldir目录。

  for(fi = 0; done == 0 && '0' + fi < 0177; fi++){ // 循环创建大文件，直到磁盘空间耗尽。
    char name[32]; // 文件名缓冲区。
    name[0] = 'b'; // 构造文件名。
    name[1] = 'i';
    name[2] = 'g';
    name[3] = '0' + fi;
    name[4] = '\0';
    unlink(name); // 删除可能存在的文件。
    int fd = open(name, O_CREATE|O_RDWR|O_TRUNC); // 创建并打开文件。
    if(fd < 0){ // 如果打开失败。
      // oops, ran out of inodes before running out of blocks.
      // 糟糕，在块耗尽之前inode就耗尽了。
      printf("%s: could not create file %s\n", s, name); // 打印错误信息。
      done = 1; // 设置完成标志。
      break; // 跳出循环。
    }
    for(int i = 0; i < MAXFILE; i++){ // 循环写入MAXFILE个块。
      char buf[BSIZE]; // 缓冲区。
      if(write(fd, buf, BSIZE) != BSIZE){ // 写入一个块。
        done = 1; // 设置完成标志。
        close(fd); // 关闭文件。
        break; // 跳出循环。
      }
    }
    close(fd); // 关闭文件。
  }

  // 现在没有空闲块了，测试如果dirlink()无法扩展
  // 目录内容，它只是失败（而不是恐慌）。
  // 预计其中一个文件创建会失败。
  int nzz = 128; // 定义要创建的文件数量。
  for(int i = 0; i < nzz; i++){ // 循环创建小文件。
    char name[32]; // 文件名缓冲区。
    name[0] = 'z'; // 构造文件名。
    name[1] = 'z';
    name[2] = '0' + (i / 32);
    name[3] = '0' + (i % 32);
    name[4] = '\0';
    unlink(name); // 删除可能存在的文件。
    int fd = open(name, O_CREATE|O_RDWR|O_TRUNC); // 创建并打开文件。
    if(fd < 0) // 如果创建失败。
      break; // 跳出循环。
    close(fd); // 关闭文件。
  }

  // 这个mkdir()预计会失败。
  if(mkdir("diskfulldir") == 0) // 尝试创建目录（应该失败）。
    printf("%s: mkdir(diskfulldir) unexpectedly succeeded!\n", s); // 打印错误信息。

  unlink("diskfulldir"); // 删除diskfulldir。

  for(int i = 0; i < nzz; i++){ // 循环删除小文件。
    char name[32]; // 文件名缓冲区。
    name[0] = 'z'; // 构造文件名。
    name[1] = 'z';
    name[2] = '0' + (i / 32);
    name[3] = '0' + (i % 32);
    name[4] = '\0';
    unlink(name); // 删除文件。
  }

  for(int i = 0; '0' + i < 0177; i++){ // 循环删除所有大文件。
    char name[32]; // 文件名缓冲区。
    name[0] = 'b'; // 构造文件名。
    name[1] = 'i';
    name[2] = 'g';
    name[3] = '0' + i;
    name[4] = '\0';
    unlink(name); // 删除文件。
  }
}

void
outofinodes(char *s) // outofinodes测试函数，测试inode耗尽的情况。
{
  int nzz = 32*32; // 定义要创建的文件数量。
  for(int i = 0; i < nzz; i++){ // 循环创建文件。
    char name[32]; // 文件名缓冲区。
    name[0] = 'z'; // 构造文件名。
    name[1] = 'z';
    name[2] = '0' + (i / 32);
    name[3] = '0' + (i % 32);
    name[4] = '\0';
    unlink(name); // 删除可能存在的文件。
    int fd = open(name, O_CREATE|O_RDWR|O_TRUNC); // 创建并打开文件。
    if(fd < 0){ // 如果创建失败。
      // failure is eventually expected.
      // 最终会预期失败。
      break; // 跳出循环。
    }
    close(fd); // 关闭文件。
  }

  for(int i = 0; i < nzz; i++){ // 循环删除所有创建的文件。
    char name[32]; // 文件名缓冲区。
    name[0] = 'z'; // 构造文件名。
    name[1] = 'z';
    name[2] = '0' + (i / 32);
    name[3] = '0' + (i % 32);
    name[4] = '\0';
    unlink(name); // 删除文件。
  }
}

// 定义一个结构体数组，包含所有“慢速”测试的函数指针和名称。
struct test slowtests[] = {
  {bigdir, "bigdir"},
  {manywrites, "manywrites"},
  {badwrite, "badwrite" },
  {execout, "execout"},
  {diskfull, "diskfull"},
  {outofinodes, "outofinodes"},

  { 0, 0}, // 数组结束标志。
};

//
// 驱动测试
//

// 在自己的进程中运行每个测试。如果子进程的exit()指示成功，run返回1。
int
run(void f(char *), char *s) { // run函数，执行单个测试。
  int pid; // 进程ID。
  int xstatus; // 退出状态。

  printf("test %s: ", s); // 打印测试名称。
  if((pid = fork()) < 0) { // fork一个子进程。
    printf("runtest: fork error\n"); // 打印错误信息。
    exit(1); // 退出并报告失败。
  }
  if(pid == 0) { // 子进程。
    f(s); // 调用实际的测试函数。
    exit(0); // 成功退出。
  } else { // 父进程。
    wait(&xstatus); // 等待子进程退出。
    if(xstatus != 0) // 如果子进程退出状态不为0。
      printf("FAILED\n"); // 打印"FAILED"。
    else
      printf("OK\n"); // 打印"OK"。
    return xstatus == 0; // 返回子进程是否成功。
  }
}

int
runtests(struct test *tests, char *justone, int continuous) { // runtests函数，运行一组测试。
  int ntests = 0; // 测试计数。
  for (struct test *t = tests; t->s != 0; t++) { // 遍历测试数组。
    if((justone == 0) || strcmp(t->s, justone) == 0) { // 如果没有指定运行特定测试，或当前测试名称匹配。
      ntests++; // 增加测试计数。
      if(!run(t->f, t->s)){ // 运行测试。
        if(continuous != 2){ // 如果不是连续模式2（忽略失败继续）。
          printf("SOME TESTS FAILED\n"); // 打印失败信息。
          return -1; // 返回-1表示有测试失败。
        }
      }
    }
  }
  return ntests; // 返回运行的测试数量。
}


// 使用sbrk()计算有多少空闲物理内存页面。
int
countfree() // countfree函数。
{
  int n = 0; // 计数器。
  uint64 sz0 = (uint64)sbrk(0); // 保存初始堆顶部地址。
  while(1){ // 循环分配内存。
    char *a = sbrk(PGSIZE); // 分配一个页。
    if(a == SBRK_ERROR){ // 如果分配失败。
      break; // 跳出循环。
    }
    n += 1; // 增加计数。
  }
  sbrk(-((uint64)sbrk(0) - sz0)); // 释放所有分配的内存，恢复堆大小。
  return n; // 返回空闲页面数量。
}

int
drivetests(int quick, int continuous, char *justone) { // drivetests函数，驱动所有测试。
  do { // 循环执行测试（如果continuous为真）。
    printf("usertests starting\n"); // 打印信息。
    int free0 = countfree(); // 记录初始空闲页面数量。
    int free1 = 0; // 结束时空闲页面数量。
    int ntests = 0; // 测试计数。
    int n; // 临时变量。
    n = runtests(quicktests, justone, continuous); // 运行快速测试。
    if (n < 0) { // 如果有测试失败。
      if(continuous != 2) { // 如果不是连续模式2。
        return 1; // 返回1表示失败。
      }
    } else {
      ntests += n; // 累加测试数量。
    }
    if(!quick) { // 如果不是快速模式。
      if (justone == 0) // 如果没有指定特定测试。
        printf("usertests slow tests starting\n"); // 打印慢速测试开始信息。
      n = runtests(slowtests, justone, continuous); // 运行慢速测试。
      if (n < 0) { // 如果有测试失败。
        if(continuous != 2) { // 如果不是连续模式2。
          return 1; // 返回1表示失败。
        }
      } else {
        ntests += n; // 累加测试数量。
      }
    }
    if((free1 = countfree()) < free0) { // 检查是否有内存泄露。
      printf("FAILED -- lost some free pages %d (out of %d)\n", free1, free0); // 打印失败信息。
      if(continuous != 2) { // 如果不是连续模式2。
        return 1; // 返回1表示失败。
      }
    }
    if (justone != 0 && ntests == 0) { // 如果指定了特定测试但没有运行任何测试。
      printf("NO TESTS EXECUTED\n"); // 打印信息。
      return 1; // 返回1表示失败。
    }
  } while(continuous); // 如果是连续模式，则继续循环。
  return 0; // 返回0表示成功。
}

int
main(int argc, char *argv[]) // main函数，程序入口。
{
  int continuous = 0; // 连续运行标志。
  int quick = 0; // 快速运行标志。
  char *justone = 0; // 指定运行的测试名称。

  if(argc == 2 && strcmp(argv[1], "-q") == 0){ // 如果参数为-q。
    quick = 1; // 设置快速运行标志。
  } else if(argc == 2 && strcmp(argv[1], "-c") == 0){ // 如果参数为-c。
    continuous = 1; // 设置连续运行标志（失败停止）。
  } else if(argc == 2 && strcmp(argv[1], "-C") == 0){ // 如果参数为-C。
    continuous = 2; // 设置连续运行标志（失败继续）。
  } else if(argc == 2 && argv[1][0] != '-'){ // 如果参数只有一个且不是选项。
    justone = argv[1]; // 将其作为指定运行的测试名称。
  } else if(argc > 1){ // 如果有多个参数且不符合上述规则。
    printf("Usage: usertests [-c] [-C] [-q] [testname]\n"); // 打印使用说明。
    exit(1); // 退出并报告失败。
  }
  if (drivetests(quick, continuous, justone)) { // 运行所有测试。
    exit(1); // 如果测试失败，则退出并报告失败。
  }
  printf("ALL TESTS PASSED\n"); // 打印所有测试通过信息。
  exit(0); // 成功退出。
}
// 这个程序运行一系列用户级测试，以验证操作系统内核的各种功能。