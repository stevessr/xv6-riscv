#include "kernel/param.h"
#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/fs.h"
#include "kernel/fcntl.h"
#include "kernel/syscall.h"
#include "kernel/memlayout.h"
#include "kernel/riscv.h"

//
// xv6 系统调用测试。
// 不带参数运行 usertests 会执行所有测试。
// 带参数 usertests <name> 会运行名为 <name> 的测试。
// 测试运行器为每个测试创建一个进程，并根据进程的退出状态，
// 报告 "OK" 或 "FAILED"。某些测试会导致内核打印 usertrap 消息，
// 如果测试打印 "OK"，这些消息可以忽略。
//

#define BUFSZ  ((MAXOPBLOCKS+2)*BSIZE)

char buf[BUFSZ];

//
// 运行速度相对较快的测试部分。如果只想运行这些测试，请使用 -q 标志。
// 不带 -q 标志，usertests 也会运行那些耗时较长的测试。
//

// 测试：向使用 copyin 读取用户内存的系统调用传递无效指针
void
copyin(char *s)
{
  uint64 addrs[] = { 0x80000000LL, 0x3fffffe000, 0x3ffffff000, 0x4000000000,
                     0xffffffffffffffff };

  for(int ai = 0; ai < sizeof(addrs)/sizeof(addrs[0]); ai++){
    uint64 addr = addrs[ai];
    
    int fd = open("copyin1", O_CREATE|O_WRONLY);
    if(fd < 0){
      printf("open(copyin1) 失败\n");
      exit(1);
    }
    int n = write(fd, (void*)addr, 8192);
    if(n >= 0){
      printf("write(fd, %p, 8192) returned %d, not -1\n", (void*)addr, n);
      exit(1);
    }
    close(fd);
    unlink("copyin1");
    
    n = write(1, (char*)addr, 8192);
    if(n > 0){
      printf("write(1, %p, 8192) returned %d, not -1 or 0\n", (void*)addr, n);
      exit(1);
    }
    
    int fds[2];
    if(pipe(fds) < 0){
      printf("pipe() 失败\n");
      exit(1);
    }
    n = write(fds[1], (char*)addr, 8192);
    if(n > 0){
      printf("write(pipe, %p, 8192) returned %d, not -1 or 0\n", (void*)addr, n);
      exit(1);
    }
    close(fds[0]);
    close(fds[1]);
  }
}

// 测试：向使用 copyout 写入用户内存的系统调用传递无效指针
void
copyout(char *s)
{
  uint64 addrs[] = { 0LL, 0x80000000LL, 0x3fffffe000, 0x3ffffff000, 0x4000000000,
                     0xffffffffffffffff };

  for(int ai = 0; ai < sizeof(addrs)/sizeof(addrs[0]); ai++){
    uint64 addr = addrs[ai];

    int fd = open("README", 0);
    if(fd < 0){
      printf("open(README) 失败\n");
      exit(1);
    }
    int n = read(fd, (void*)addr, 8192);
    if(n > 0){
      printf("read(fd, %p, 8192) returned %d, not -1 or 0\n", (void*)addr, n);
      exit(1);
    }
    close(fd);

    int fds[2];
    if(pipe(fds) < 0){
      printf("pipe() 失败\n");
      exit(1);
    }
    n = write(fds[1], "x", 1);
    if(n != 1){
      printf("pipe write 失败\n");
      exit(1);
    }
    n = read(fds[0], (void*)addr, 8192);
    if(n > 0){
      printf("read(pipe, %p, 8192) returned %d, not -1 or 0\n", (void*)addr, n);
      exit(1);
    }
    close(fds[0]);
    close(fds[1]);
  }
}

// 测试：向系统调用传递无效的字符串指针
void
copyinstr1(char *s)
{
  uint64 addrs[] = { 0x80000000LL, 0x3fffffe000, 0x3ffffff000, 0x4000000000,
                     0xffffffffffffffff };

  for(int ai = 0; ai < sizeof(addrs)/sizeof(addrs[0]); ai++){
    uint64 addr = addrs[ai];

    int fd = open((char *)addr, O_CREATE|O_WRONLY);
    if(fd >= 0){
      printf("open(%p) returned %d, not -1\n", (void*)addr, fd);
      exit(1);
    }
  }
}

// 测试：当字符串系统调用参数的长度恰好是内核缓冲区的大小时，
// 会发生什么，此时空字符会落在内核缓冲区的末尾之外。
void
copyinstr2(char *s)
{
  char b[MAXPATH+1];

  for(int i = 0; i < MAXPATH; i++)
    b[i] = 'x';
  b[MAXPATH] = '\0';
  
  int ret = unlink(b);
  if(ret != -1){
    printf("unlink(%s) returned %d, not -1\n", b, ret);
    exit(1);
  }

  int fd = open(b, O_CREATE | O_WRONLY);
  if(fd != -1){
    printf("open(%s) returned %d, not -1\n", b, fd);
    exit(1);
  }

  ret = link(b, b);
  if(ret != -1){
    printf("link(%s, %s) returned %d, not -1\n", b, b, ret);
    exit(1);
  }

  char *args[] = { "xx", 0 };
  ret = exec(b, args);
  if(ret != -1){
    printf("exec(%s) returned %d, not -1\n", b, fd);
    exit(1);
  }

  int pid = fork();
  if(pid < 0){
    printf("fork 失败\n");
    exit(1);
  }
  if(pid == 0){
    static char big[PGSIZE+1];
    for(int i = 0; i < PGSIZE; i++)
      big[i] = 'x';
    big[PGSIZE] = '\0';
    char *args2[] = { big, big, big, 0 };
    ret = exec("echo", args2);
    if(ret != -1){
      printf("exec(echo, BIG) returned %d, not -1\n", fd);
      exit(1);
    }
    exit(747); // OK
  }

  int st = 0;
  wait(&st);
  if(st != 747){
    printf("exec(echo, BIG) succeeded, should have 失败\n");
    exit(1);
  }
}

// 测试：当字符串参数跨越最后一个用户页的末尾时会发生什么？
void
copyinstr3(char *s)
{
  sbrk(8192);
  uint64 top = (uint64) sbrk(0);
  if((top % PGSIZE) != 0){
    sbrk(PGSIZE - (top % PGSIZE));
  }
  top = (uint64) sbrk(0);
  if(top % PGSIZE){
    printf("oops\n");
    exit(1);
  }

  char *b = (char *) (top - 1);
  *b = 'x';

  int ret = unlink(b);
  if(ret != -1){
    printf("unlink(%s) returned %d, not -1\n", b, ret);
    exit(1);
  }

  int fd = open(b, O_CREATE | O_WRONLY);
  if(fd != -1){
    printf("open(%s) returned %d, not -1\n", b, fd);
    exit(1);
  }

  ret = link(b, b);
  if(ret != -1){
    printf("link(%s, %s) returned %d, not -1\n", b, b, ret);
    exit(1);
  }

  char *args[] = { "xx", 0 };
  ret = exec(b, args);
  if(ret != -1){
    printf("exec(%s) returned %d, not -1\n", b, fd);
    exit(1);
  }
}

// 测试：内核是否拒绝读/写应用程序已经通过sbrk释放的用户内存。
void
rwsbrk(char *s)
{
  int fd, n;
  
  uint64 a = (uint64) sbrk(8192);

  if(a == 0xffffffffffffffffLL) {
    printf("sbrk(rwsbrk) 失败\n");
    exit(1);
  }
  
  if ((uint64) sbrk(-8192) ==  0xffffffffffffffffLL) {
    printf("sbrk(rwsbrk) shrink 失败\n");
    exit(1);
  }

  fd = open("rwsbrk", O_CREATE|O_WRONLY);
  if(fd < 0){
    printf("open(rwsbrk) 失败\n");
    exit(1);
  }
  n = write(fd, (void*)(a+4096), 1024);
  if(n >= 0){
    printf("write(fd, %p, 1024) returned %d, not -1\n", (void*)a+4096, n);
    exit(1);
  }
  close(fd);
  unlink("rwsbrk");

  fd = open("README", O_RDONLY);
  if(fd < 0){
    printf("open(rwsbrk) 失败\n");
    exit(1);
  }
  n = read(fd, (void*)(a+4096), 10);
  if(n >= 0){
    printf("read(fd, %p, 10) returned %d, not -1\n", (void*)a+4096, n);
    exit(1);
  }
  close(fd);
  
  exit(0);
}

// 测试 O_TRUNC。
void
truncate1(char *s)
{
  char buf[32];
  
  unlink("truncfile");
  int fd1 = open("truncfile", O_CREATE|O_WRONLY|O_TRUNC);
  write(fd1, "abcd", 4);
  close(fd1);

  int fd2 = open("truncfile", O_RDONLY);
  int n = read(fd2, buf, sizeof(buf));
  if(n != 4){
    printf("%s: read %d bytes, wanted 4\n", s, n);
    exit(1);
  }

  fd1 = open("truncfile", O_WRONLY|O_TRUNC);

  int fd3 = open("truncfile", O_RDONLY);
  n = read(fd3, buf, sizeof(buf));
  if(n != 0){
    printf("aaa fd3=%d\n", fd3);
    printf("%s: read %d bytes, wanted 0\n", s, n);
    exit(1);
  }

  n = read(fd2, buf, sizeof(buf));
  if(n != 0){
    printf("bbb fd2=%d\n", fd2);
    printf("%s: read %d bytes, wanted 0\n", s, n);
    exit(1);
  }
  
  write(fd1, "abcdef", 6);

  n = read(fd3, buf, sizeof(buf));
  if(n != 6){
    printf("%s: read %d bytes, wanted 6\n", s, n);
    exit(1);
  }

  n = read(fd2, buf, sizeof(buf));
  if(n != 2){
    printf("%s: read %d bytes, wanted 2\n", s, n);
    exit(1);
  }

  unlink("truncfile");

  close(fd1);
  close(fd2);
  close(fd3);
}

// 测试：写入一个刚被截断的打开的文件描述符。
// 这会导致在文件末尾之外的偏移量处进行写入。
// 这种写入在 xv6 上会失败（与 POSIX 不同），但至少它们不会崩溃。
void
truncate2(char *s)
{
  unlink("truncfile");

  int fd1 = open("truncfile", O_CREATE|O_TRUNC|O_WRONLY);
  write(fd1, "abcd", 4);

  int fd2 = open("truncfile", O_TRUNC|O_WRONLY);

  int n = write(fd1, "x", 1);
  if(n != -1){
    printf("%s: write returned %d, expected -1\n", s, n);
    exit(1);
  }

  unlink("truncfile");
  close(fd1);
  close(fd2);
}

// 并发截断测试
void
truncate3(char *s)
{
  int pid, xstatus;

  close(open("truncfile", O_CREATE|O_TRUNC|O_WRONLY));
  
  pid = fork();
  if(pid < 0){
    printf("%s: fork 失败\n", s);
    exit(1);
  }

  if(pid == 0){
    for(int i = 0; i < 100; i++){
      char buf[32];
      int fd = open("truncfile", O_WRONLY);
      if(fd < 0){
        printf("%s: open 失败\n", s);
        exit(1);
      }
      int n = write(fd, "1234567890", 10);
      if(n != 10){
        printf("%s: write got %d, expected 10\n", s, n);
        exit(1);
      }
      close(fd);
      fd = open("truncfile", O_RDONLY);
      read(fd, buf, sizeof(buf));
      close(fd);
    }
    exit(0);
  }

  for(int i = 0; i < 150; i++){
    int fd = open("truncfile", O_CREATE|O_WRONLY|O_TRUNC);
    if(fd < 0){
      printf("%s: open 失败\n", s);
      exit(1);
    }
    int n = write(fd, "xxx", 3);
    if(n != 3){
      printf("%s: write got %d, expected 3\n", s, n);
      exit(1);
    }
    close(fd);
  }

  wait(&xstatus);
  unlink("truncfile");
  exit(xstatus);
}
  

// 测试：chdir() 是否在事务中调用 iput(p->cwd)？
void
iputtest(char *s)
{
  if(mkdir("iputdir") < 0){
    printf("%s: mkdir 失败\n", s);
    exit(1);
  }
  if(chdir("iputdir") < 0){
    printf("%s: chdir iputdir 失败\n", s);
    exit(1);
  }
  if(unlink("../iputdir") < 0){
    printf("%s: unlink ../iputdir 失败\n", s);
    exit(1);
  }
  if(chdir("/") < 0){
    printf("%s: chdir / 失败\n", s);
    exit(1);
  }
}

// 测试：exit() 是否在事务中调用 iput(p->cwd)？
void
exitiputtest(char *s)
{
  int pid, xstatus;

  pid = fork();
  if(pid < 0){
    printf("%s: fork 失败\n", s);
    exit(1);
  }
  if(pid == 0){
    if(mkdir("iputdir") < 0){
      printf("%s: mkdir 失败\n", s);
      exit(1);
    }
    if(chdir("iputdir") < 0){
      printf("%s: child chdir 失败\n", s);
      exit(1);
    }
    if(unlink("../iputdir") < 0){
      printf("%s: unlink ../iputdir 失败\n", s);
      exit(1);
    }
    exit(0);
  }
  wait(&xstatus);
  exit(xstatus);
}

// 测试：open() 中尝试写入目录的错误路径是否在事务中调用 iput()？
// 需要一个被修改过的内核，在 sys_open() 中的 namei() 调用后暂停：
//    if((ip = namei(path)) == 0)
//      return -1;
//    {
//      int i;
//      for(i = 0; i < 10000; i++)
//        yield();
//    }
void
openiputtest(char *s)
{
  int pid, xstatus;

  if(mkdir("oidir") < 0){
    printf("%s: mkdir oidir 失败\n", s);
    exit(1);
  }
  pid = fork();
  if(pid < 0){
    printf("%s: fork 失败\n", s);
    exit(1);
  }
  if(pid == 0){
    int fd = open("oidir", O_RDWR);
    if(fd >= 0){
      printf("%s: open directory for write succeeded\n", s);
      exit(1);
    }
    exit(0);
  }
  sleep(1);
  if(unlink("oidir") != 0){
    printf("%s: unlink 失败\n", s);
    exit(1);
  }
  wait(&xstatus);
  exit(xstatus);
}

// 简单的文件系统测试

// 打开文件测试
void
opentest(char *s)
{
  int fd;

  fd = open("echo", 0);
  if(fd < 0){
    printf("%s: open echo 失败!\n", s);
    exit(1);
  }
  close(fd);
  fd = open("doesnotexist", 0);
  if(fd >= 0){
    printf("%s: open doesnotexist succeeded!\n", s);
    exit(1);
  }
}

// 写入小文件测试
void
writetest(char *s)
{
  int fd;
  int i;
  enum { N=100, SZ=10 };
  
  fd = open("small", O_CREATE|O_RDWR);
  if(fd < 0){
    printf("%s: error: creat small 失败!\n", s);
    exit(1);
  }
  for(i = 0; i < N; i++){
    if(write(fd, "aaaaaaaaaa", SZ) != SZ){
      printf("%s: error: write aa %d new file 失败\n", s, i);
      exit(1);
    }
    if(write(fd, "bbbbbbbbbb", SZ) != SZ){
      printf("%s: error: write bb %d new file 失败\n", s, i);
      exit(1);
    }
  }
  close(fd);
  fd = open("small", O_RDONLY);
  if(fd < 0){
    printf("%s: error: open small 失败!\n", s);
    exit(1);
  }
  i = read(fd, buf, N*SZ*2);
  if(i != N*SZ*2){
    printf("%s: read 失败\n", s);
    exit(1);
  }
  close(fd);

  if(unlink("small") < 0){
    printf("%s: unlink small 失败\n", s);
    exit(1);
  }
}

// 写入大文件测试
void
writebig(char *s)
{
  int i, fd, n;

  fd = open("big", O_CREATE|O_RDWR);
  if(fd < 0){
    printf("%s: error: creat big 失败!\n", s);
    exit(1);
  }

  for(i = 0; i < MAXFILE; i++){
    ((int*)buf)[0] = i;
    if(write(fd, buf, BSIZE) != BSIZE){
      printf("%s: error: write big file 失败 i=%d\n", s, i);
      exit(1);
    }
  }

  close(fd);

  fd = open("big", O_RDONLY);
  if(fd < 0){
    printf("%s: error: open big 失败!\n", s);
    exit(1);
  }

  n = 0;
  for(;;){
    i = read(fd, buf, BSIZE);
    if(i == 0){
      if(n != MAXFILE){
        printf("%s: read only %d blocks from big", s, n);
        exit(1);
      }
      break;
    } else if(i != BSIZE){
      printf("%s: read 失败 %d\n", s, i);
      exit(1);
    }
    if(((int*)buf)[0] != n){
      printf("%s: read content of block %d is %d\n", s,
             n, ((int*)buf)[0]);
      exit(1);
    }
    n++;
  }
  close(fd);
  if(unlink("big") < 0){
    printf("%s: unlink big 失败\n", s);
    exit(1);
  }
}

// 大量创建文件，然后进行 unlink 测试
void
createtest(char *s)
{
  int i, fd;
  enum { N=52 };

  char name[3];
  name[0] = 'a';
  name[2] = '\0';
  for(i = 0; i < N; i++){
    name[1] = '0' + i;
    fd = open(name, O_CREATE|O_RDWR);
    close(fd);
  }
  name[0] = 'a';
  name[2] = '\0';
  for(i = 0; i < N; i++){
    name[1] = '0' + i;
    unlink(name);
  }
}

// 目录操作测试
void dirtest(char *s)
{
  if(mkdir("dir0") < 0){
    printf("%s: mkdir 失败\n", s);
    exit(1);
  }

  if(chdir("dir0") < 0){
    printf("%s: chdir dir0 失败\n", s);
    exit(1);
  }

  if(chdir("..") < 0){
    printf("%s: chdir .. 失败\n", s);
    exit(1);
  }

  if(unlink("dir0") < 0){
    printf("%s: unlink dir0 失败\n", s);
    exit(1);
  }
}

// exec 测试
void
exectest(char *s)
{
  int fd, xstatus, pid;
  char *echoargv[] = { "echo", "OK", 0 };
  char buf[3];

  unlink("echo-ok");
  pid = fork();
  if(pid < 0) {
     printf("%s: fork 失败\n", s);
     exit(1);
  }
  if(pid == 0) {
    close(1);
    fd = open("echo-ok", O_CREATE|O_WRONLY);
    if(fd < 0) {
      printf("%s: create 失败\n", s);
      exit(1);
    }
    if(fd != 1) {
      printf("%s: wrong fd\n", s);
      exit(1);
    }
    if(exec("echo", echoargv) < 0){
      printf("%s: exec echo 失败\n", s);
      exit(1);
    }
    // 不会执行到这里
  }
  if (wait(&xstatus) != pid) {
    printf("%s: wait 失败!\n", s);
  }
  if(xstatus != 0)
    exit(xstatus);

  fd = open("echo-ok", O_RDONLY);
  if(fd < 0) {
    printf("%s: open 失败\n", s);
    exit(1);
  }
  if (read(fd, buf, 2) != 2) {
    printf("%s: read 失败\n", s);
    exit(1);
  }
  unlink("echo-ok");
  if(buf[0] == 'O' && buf[1] == 'K')
    exit(0);
  else {
    printf("%s: wrong output\n", s);
    exit(1);
  }

}

// 简单的 fork 和 pipe 读/写测试
void
pipe1(char *s)
{
  int fds[2], pid, xstatus;
  int seq, i, n, cc, total;
  enum { N=5, SZ=1033 };
  
  if(pipe(fds) != 0){
    printf("%s: pipe() 失败\n", s);
    exit(1);
  }
  pid = fork();
  seq = 0;
  if(pid == 0){
    close(fds[0]);
    for(n = 0; n < N; n++){
      for(i = 0; i < SZ; i++)
        buf[i] = seq++;
      if(write(fds[1], buf, SZ) != SZ){
        printf("%s: pipe1 oops 1\n", s);
        exit(1);
      }
    }
    exit(0);
  } else if(pid > 0){
    close(fds[1]);
    total = 0;
    cc = 1;
    while((n = read(fds[0], buf, cc)) > 0){
      for(i = 0; i < n; i++){
        if((buf[i] & 0xff) != (seq++ & 0xff)){
          printf("%s: pipe1 oops 2\n", s);
          return;
        }
      }
      total += n;
      cc = cc * 2;
      if(cc > sizeof(buf))
        cc = sizeof(buf);
    }
    if(total != N * SZ){
      printf("%s: pipe1 oops 3 total %d\n", s, total);
      exit(1);
    }
    close(fds[0]);
    wait(&xstatus);
    exit(xstatus);
  } else {
    printf("%s: fork() 失败\n", s);
    exit(1);
  }
}


// 测试子进程被杀死时（状态 = -1）
void
killstatus(char *s)
{
  int xst;
  
  for(int i = 0; i < 100; i++){
    int pid1 = fork();
    if(pid1 < 0){
      printf("%s: fork 失败\n", s);
      exit(1);
    }
    if(pid1 == 0){
      while(1) {
        getpid();
      }
      exit(0);
    }
    sleep(1);
    kill(pid1);
    wait(&xst);
    if(xst != -1) {
       printf("%s: status should be -1\n", s);
       exit(1);
    }
  }
  exit(0);
}

// 抢占测试，设计在最多两个 CPU 的环境下运行
void
preempt(char *s)
{
  int pid1, pid2, pid3;
  int pfds[2];

  pid1 = fork();
  if(pid1 < 0) {
    printf("%s: fork 失败", s);
    exit(1);
  }
  if(pid1 == 0)
    for(;;)
      ;

  pid2 = fork();
  if(pid2 < 0) {
    printf("%s: fork 失败\n", s);
    exit(1);
  }
  if(pid2 == 0)
    for(;;)
      ;

  pipe(pfds);
  pid3 = fork();
  if(pid3 < 0) {
     printf("%s: fork 失败\n", s);
     exit(1);
  }
  if(pid3 == 0){
    close(pfds[0]);
    if(write(pfds[1], "x", 1) != 1)
      printf("%s: preempt write error", s);
    close(pfds[1]);
    for(;;)
      ;
  }

  close(pfds[1]);
  if(read(pfds[0], buf, sizeof(buf)) != 1){
    printf("%s: preempt read error", s);
    return;
  }
  close(pfds[0]);
  printf("kill... ");
  kill(pid1);
  kill(pid2);
  kill(pid3);
  printf("wait... ");
  wait(0);
  wait(0);
  wait(0);
}

// 尝试查找 exit 和 wait 之间的任何竞争条件
void
exitwait(char *s)
{
  int i, pid;

  for(i = 0; i < 100; i++){
    pid = fork();
    if(pid < 0){
      printf("%s: fork 失败\n", s);
      exit(1);
    }
    if(pid){
      int xstate;
      if(wait(&xstate) != pid){
        printf("%s: wait wrong pid\n", s);
        exit(1);
      }
      if(i != xstate) {
        printf("%s: wait wrong exit status\n", s);
        exit(1);
      }
    } else {
      exit(i);
    }
  }
}

// 尝试在处理父进程退出时仍有存活子进程的
// 重新认领父进程的代码中发现竞争条件。
void
reparent(char *s)
{
  int master_pid = getpid();
  for(int i = 0; i < 200; i++){
    int pid = fork();
    if(pid < 0){
      printf("%s: fork 失败\n", s);
      exit(1);
    }
    if(pid){
      if(wait(0) != pid){
        printf("%s: wait wrong pid\n", s);
        exit(1);
      }
    } else {
      int pid2 = fork();
      if(pid2 < 0){
        kill(master_pid);
        exit(1);
      }
      exit(0);
    }
  }
  exit(0);
}

// 测试：如果两个子进程同时 exit() 会发生什么？
void
twochildren(char *s)
{
  for(int i = 0; i < 1000; i++){
    int pid1 = fork();
    if(pid1 < 0){
      printf("%s: fork 失败\n", s);
      exit(1);
    }
    if(pid1 == 0){
      exit(0);
    } else {
      int pid2 = fork();
      if(pid2 < 0){
        printf("%s: fork 失败\n", s);
        exit(1);
      }
      if(pid2 == 0){
        exit(0);
      } else {
        wait(0);
        wait(0);
      }
    }
  }
}

// 并发 fork 以尝试暴露锁错误。
void
forkfork(char *s)
{
  enum { N=2 };
  
  for(int i = 0; i < N; i++){
    int pid = fork();
    if(pid < 0){
      printf("%s: fork 失败", s);
      exit(1);
    }
    if(pid == 0){
      for(int j = 0; j < 200; j++){
        int pid1 = fork();
        if(pid1 < 0){
          exit(1);
        }
        if(pid1 == 0){
          exit(0);
        }
        wait(0);
      }
      exit(0);
    }
  }

  int xstatus;
  for(int i = 0; i < N; i++){
    wait(&xstatus);
    if(xstatus != 0) {
      printf("%s: fork in child 失败", s);
      exit(1);
    }
  }
}

// fork 压力测试
void
forkforkfork(char *s)
{
  unlink("stopforking");

  int pid = fork();
  if(pid < 0){
    printf("%s: fork 失败", s);
    exit(1);
  }
  if(pid == 0){
    while(1){
      int fd = open("stopforking", 0);
      if(fd >= 0){
        exit(0);
      }
      if(fork() < 0){
        close(open("stopforking", O_CREATE|O_RDWR));
      }
    }

    exit(0);
  }

  sleep(20); // two seconds
  close(open("stopforking", O_CREATE|O_RDWR));
  wait(0);
  sleep(10); // one second
}

// 回归测试。reparent() 在将子进程交给 init 时是否违反了
// 先父后子的锁定顺序，从而导致 exit() 与 init 的 wait() 死锁？
// 也用于触发 "panic: release"，因为 exit() 释放了与其获取的不同的 p->parent->lock。
void
reparent2(char *s)
{
  for(int i = 0; i < 800; i++){
    int pid1 = fork();
    if(pid1 < 0){
      printf("fork 失败\n");
      exit(1);
    }
    if(pid1 == 0){
      fork();
      fork();
      exit(0);
    }
    wait(0);
  }

  exit(0);
}

// 分配所有内存，释放它，然后再次分配
void
mem(char *s)
{
  void *m1, *m2;
  int pid;

  if((pid = fork()) == 0){
    m1 = 0;
    while((m2 = malloc(10001)) != 0){
      *(char**)m2 = m1;
      m1 = m2;
    }
    while(m1){
      m2 = *(char**)m1;
      free(m1);
      m1 = m2;
    }
    m1 = malloc(1024*20);
    if(m1 == 0){
      printf("%s: couldn't allocate mem?!!\n", s);
      exit(1);
    }
    free(m1);
    exit(0);
  } else {
    int xstatus;
    wait(&xstatus);
    if(xstatus == -1){
      // 可能是页面错误，所以可能是 lazy lab，
      // 所以没关系。
      exit(0);
    }
    exit(xstatus);
  }
}

// 更多文件系统测试

// 两个进程写入同一个文件描述符
// 偏移量是共享的吗？inode 锁定是否有效？
void
sharedfd(char *s)
{
  int fd, pid, i, n, nc, np;
  enum { N = 1000, SZ=10};
  char buf[SZ];

  unlink("sharedfd");
  fd = open("sharedfd", O_CREATE|O_RDWR);
  if(fd < 0){
    printf("%s: cannot open sharedfd for writing", s);
    exit(1);
  }
  pid = fork();
  memset(buf, pid==0?'c':'p', sizeof(buf));
  for(i = 0; i < N; i++){
    if(write(fd, buf, sizeof(buf)) != sizeof(buf)){
      printf("%s: write sharedfd 失败\n", s);
      exit(1);
    }
  }
  if(pid == 0) {
    exit(0);
  } else {
    int xstatus;
    wait(&xstatus);
    if(xstatus != 0)
      exit(xstatus);
  }
  
  close(fd);
  fd = open("sharedfd", 0);
  if(fd < 0){
    printf("%s: cannot open sharedfd for reading\n", s);
    exit(1);
  }
  nc = np = 0;
  while((n = read(fd, buf, sizeof(buf))) > 0){
    for(i = 0; i < sizeof(buf); i++){
      if(buf[i] == 'c')
        nc++;
      if(buf[i] == 'p')
        np++;
    }
  }
  close(fd);
  unlink("sharedfd");
  if(nc == N*SZ && np == N*SZ){
    exit(0);
  } else {
    printf("%s: nc/np test fails\n", s);
    exit(1);
  }
}

// 四个进程同时写入不同的文件，以测试块分配。
void
fourfiles(char *s)
{
  int fd, pid, i, j, n, total, pi;
  char *names[] = { "f0", "f1", "f2", "f3" };
  char *fname;
  enum { N=12, NCHILD=4, SZ=500 };
  
  for(pi = 0; pi < NCHILD; pi++){
    fname = names[pi];
    unlink(fname);

    pid = fork();
    if(pid < 0){
      printf("%s: fork 失败\n", s);
      exit(1);
    }

    if(pid == 0){
      fd = open(fname, O_CREATE | O_RDWR);
      if(fd < 0){
        printf("%s: create 失败\n", s);
        exit(1);
      }

      memset(buf, '0'+pi, SZ);
      for(i = 0; i < N; i++){
        if((n = write(fd, buf, SZ)) != SZ){
          printf("write 失败 %d\n", n);
          exit(1);
        }
      }
      exit(0);
    }
  }

  int xstatus;
  for(pi = 0; pi < NCHILD; pi++){
    wait(&xstatus);
    if(xstatus != 0)
      exit(xstatus);
  }

  for(i = 0; i < NCHILD; i++){
    fname = names[i];
    fd = open(fname, 0);
    total = 0;
    while((n = read(fd, buf, sizeof(buf))) > 0){
      for(j = 0; j < n; j++){
        if(buf[j] != '0'+i){
          printf("%s: wrong char\n", s);
          exit(1);
        }
      }
      total += n;
    }
    close(fd);
    if(total != N*SZ){
      printf("wrong length %d\n", total);
      exit(1);
    }
    unlink(fname);
  }
}

// 四个进程在同一目录中创建和删除不同的文件
void
createdelete(char *s)
{
  enum { N = 20, NCHILD=4 };
  int pid, i, fd, pi;
  char name[32];

  for(pi = 0; pi < NCHILD; pi++){
    pid = fork();
    if(pid < 0){
      printf("%s: fork 失败\n", s);
      exit(1);
    }

    if(pid == 0){
      name[0] = 'p' + pi;
      name[2] = '\0';
      for(i = 0; i < N; i++){
        name[1] = '0' + i;
        fd = open(name, O_CREATE | O_RDWR);
        if(fd < 0){
          printf("%s: create 失败\n", s);
          exit(1);
        }
        close(fd);
        if(i > 0 && (i % 2 ) == 0){
          name[1] = '0' + (i / 2);
          if(unlink(name) < 0){
            printf("%s: unlink 失败\n", s);
            exit(1);
          }
        }
      }
      exit(0);
    }
  }

  int xstatus;
  for(pi = 0; pi < NCHILD; pi++){
    wait(&xstatus);
    if(xstatus != 0)
      exit(1);
  }

  name[0] = name[1] = name[2] = 0;
  for(i = 0; i < N; i++){
    for(pi = 0; pi < NCHILD; pi++){
      name[0] = 'p' + pi;
      name[1] = '0' + i;
      fd = open(name, 0);
      if((i == 0 || i >= N/2) && fd < 0){
        printf("%s: oops createdelete %s didn't exist\n", s, name);
        exit(1);
      } else if((i >= 1 && i < N/2) && fd >= 0){
        printf("%s: oops createdelete %s did exist\n", s, name);
        exit(1);
      }
      if(fd >= 0)
        close(fd);
    }
  }

  for(i = 0; i < N; i++){
    for(pi = 0; pi < NCHILD; pi++){
      name[0] = 'p' + pi;
      name[1] = '0' + i;
      unlink(name);
    }
  }
}

// 测试：我能 unlink 一个文件然后仍然读取它吗？
void
unlinkread(char *s)
{
  enum { SZ = 5 };
  int fd, fd1;

  fd = open("unlinkread", O_CREATE | O_RDWR);
  if(fd < 0){
    printf("%s: create unlinkread 失败\n", s);
    exit(1);
  }
  write(fd, "hello", SZ);
  close(fd);

  fd = open("unlinkread", O_RDWR);
  if(fd < 0){
    printf("%s: open unlinkread 失败\n", s);
    exit(1);
  }
  if(unlink("unlinkread") != 0){
    printf("%s: unlink unlinkread 失败\n", s);
    exit(1);
  }

  fd1 = open("unlinkread", O_CREATE | O_RDWR);
  write(fd1, "yyy", 3);
  close(fd1);

  if(read(fd, buf, sizeof(buf)) != SZ){
    printf("%s: unlinkread read 失败", s);
    exit(1);
  }
  if(buf[0] != 'h'){
    printf("%s: unlinkread wrong data\n", s);
    exit(1);
  }
  if(write(fd, buf, 10) != 10){
    printf("%s: unlinkread write 失败\n", s);
    exit(1);
  }
  close(fd);
  unlink("unlinkread");
}

// 链接测试
void
linktest(char *s)
{
  enum { SZ = 5 };
  int fd;

  unlink("lf1");
  unlink("lf2");

  fd = open("lf1", O_CREATE|O_RDWR);
  if(fd < 0){
    printf("%s: create lf1 失败\n", s);
    exit(1);
  }
  if(write(fd, "hello", SZ) != SZ){
    printf("%s: write lf1 失败\n", s);
    exit(1);
  }
  close(fd);

  if(link("lf1", "lf2") < 0){
    printf("%s: link lf1 lf2 失败\n", s);
    exit(1);
  }
  unlink("lf1");

  if(open("lf1", 0) >= 0){
    printf("%s: unlinked lf1 but it is still there!\n", s);
    exit(1);
  }

  fd = open("lf2", 0);
  if(fd < 0){
    printf("%s: open lf2 失败\n", s);
    exit(1);
  }
  if(read(fd, buf, sizeof(buf)) != SZ){
    printf("%s: read lf2 失败\n", s);
    exit(1);
  }
  close(fd);

  if(link("lf2", "lf2") >= 0){
    printf("%s: link lf2 lf2 succeeded! oops\n", s);
    exit(1);
  }

  unlink("lf2");
  if(link("lf2", "lf1") >= 0){
    printf("%s: link non-existent succeeded! oops\n", s);
    exit(1);
  }

  if(link(".", "lf1") >= 0){
    printf("%s: link . lf1 succeeded! oops\n", s);
    exit(1);
  }
}

// 测试并发创建/链接/删除同一个文件
void
concreate(char *s)
{
  enum { N = 40 };
  char file[3];
  int i, pid, n, fd;
  char fa[N];
  struct {
    ushort inum;
    char name[DIRSIZ];
  } de;

  file[0] = 'C';
  file[2] = '\0';
  for(i = 0; i < N; i++){
    file[1] = '0' + i;
    unlink(file);
    pid = fork();
    if(pid && (i % 3) == 1){
      link("C0", file);
    } else if(pid == 0 && (i % 5) == 1){
      link("C0", file);
    } else {
      fd = open(file, O_CREATE | O_RDWR);
      if(fd < 0){
        printf("concreate create %s 失败\n", file);
        exit(1);
      }
      close(fd);
    }
    if(pid == 0) {
      exit(0);
    } else {
      int xstatus;
      wait(&xstatus);
      if(xstatus != 0)
        exit(1);
    }
  }

  memset(fa, 0, sizeof(fa));
  fd = open(".", 0);
  n = 0;
  while(read(fd, &de, sizeof(de)) > 0){
    if(de.inum == 0)
      continue;
    if(de.name[0] == 'C' && de.name[2] == '\0'){
      i = de.name[1] - '0';
      if(i < 0 || i >= sizeof(fa)){
        printf("%s: concreate weird file %s\n", s, de.name);
        exit(1);
      }
      if(fa[i]){
        printf("%s: concreate duplicate file %s\n", s, de.name);
        exit(1);
      }
      fa[i] = 1;
      n++;
    }
  }
  close(fd);

  if(n != N){
    printf("%s: concreate not enough files in directory listing\n", s);
    exit(1);
  }

  for(i = 0; i < N; i++){
    file[1] = '0' + i;
    pid = fork();
    if(pid < 0){
      printf("%s: fork 失败\n", s);
      exit(1);
    }
    if(((i % 3) == 0 && pid == 0) ||
       ((i % 3) == 1 && pid != 0)){
      close(open(file, 0));
      close(open(file, 0));
      close(open(file, 0));
      close(open(file, 0));
      close(open(file, 0));
      close(open(file, 0));
    } else {
      unlink(file);
      unlink(file);
      unlink(file);
      unlink(file);
      unlink(file);
      unlink(file);
    }
    if(pid == 0)
      exit(0);
    else
      wait(0);
  }
}

// 另一个并发链接/删除/创建测试，用于查找死锁。
void
linkunlink(char *s)
{
  int pid, i;

  unlink("x");
  pid = fork();
  if(pid < 0){
    printf("%s: fork 失败\n", s);
    exit(1);
  }

  unsigned int x = (pid ? 1 : 97);
  for(i = 0; i < 100; i++){
    x = x * 1103515245 + 12345;
    if((x % 3) == 0){
      close(open("x", O_RDWR | O_CREATE));
    } else if((x % 3) == 1){
      link("cat", "x");
    } else {
      unlink("x");
    }
  }

  if(pid)
    wait(0);
  else
    exit(0);
}


// 子目录测试
void
subdir(char *s)
{
  int fd, cc;

  unlink("ff");
  if(mkdir("dd") != 0){
    printf("%s: mkdir dd 失败\n", s);
    exit(1);
  }

  fd = open("dd/ff", O_CREATE | O_RDWR);
  if(fd < 0){
    printf("%s: create dd/ff 失败\n", s);
    exit(1);
  }
  write(fd, "ff", 2);
  close(fd);

  if(unlink("dd") >= 0){
    printf("%s: unlink dd (non-empty dir) succeeded!\n", s);
    exit(1);
  }

  if(mkdir("/dd/dd") != 0){
    printf("%s: subdir mkdir dd/dd 失败\n", s);
    exit(1);
  }

  fd = open("dd/dd/ff", O_CREATE | O_RDWR);
  if(fd < 0){
    printf("%s: create dd/dd/ff 失败\n", s);
    exit(1);
  }
  write(fd, "FF", 2);
  close(fd);

  fd = open("dd/dd/../ff", 0);
  if(fd < 0){
    printf("%s: open dd/dd/../ff 失败\n", s);
    exit(1);
  }
  cc = read(fd, buf, sizeof(buf));
  if(cc != 2 || buf[0] != 'f'){
    printf("%s: dd/dd/../ff wrong content\n", s);
    exit(1);
  }
  close(fd);

  if(link("dd/dd/ff", "dd/dd/ffff") != 0){
    printf("%s: link dd/dd/ff dd/dd/ffff 失败\n", s);
    exit(1);
  }

  if(unlink("dd/dd/ff") != 0){
    printf("%s: unlink dd/dd/ff 失败\n", s);
    exit(1);
  }
  if(open("dd/dd/ff", O_RDONLY) >= 0){
    printf("%s: open (unlinked) dd/dd/ff succeeded\n", s);
    exit(1);
  }

  if(chdir("dd") != 0){
    printf("%s: chdir dd 失败\n", s);
    exit(1);
  }
  if(chdir("dd/../../dd") != 0){
    printf("%s: chdir dd/../../dd 失败\n", s);
    exit(1);
  }
  if(chdir("dd/../../../dd") != 0){
    printf("%s: chdir dd/../../../dd 失败\n", s);
    exit(1);
  }
  if(chdir("./..") != 0){
    printf("%s: chdir ./.. 失败\n", s);
    exit(1);
  }

  fd = open("dd/dd/ffff", 0);
  if(fd < 0){
    printf("%s: open dd/dd/ffff 失败\n", s);
    exit(1);
  }
  if(read(fd, buf, sizeof(buf)) != 2){
    printf("%s: read dd/dd/ffff wrong len\n", s);
    exit(1);
  }
  close(fd);

  if(open("dd/dd/ff", O_RDONLY) >= 0){
    printf("%s: open (unlinked) dd/dd/ff succeeded!\n", s);
    exit(1);
  }

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

  if(unlink("dd/dd/ffff") != 0){
    printf("%s: unlink dd/dd/ff 失败\n", s);
    exit(1);
  }
  if(unlink("dd/ff") != 0){
    printf("%s: unlink dd/ff 失败\n", s);
    exit(1);
  }
  if(unlink("dd") == 0){
    printf("%s: unlink non-empty dd succeeded!\n", s);
    exit(1);
  }
  if(unlink("dd/dd") < 0){
    printf("%s: unlink dd/dd 失败\n", s);
    exit(1);
  }
  if(unlink("dd") < 0){
    printf("%s: unlink dd 失败\n", s);
    exit(1);
  }
}

// 测试大于日志大小的写入。
void
bigwrite(char *s)
{
  int fd, sz;

  unlink("bigwrite");
  for(sz = 499; sz < (MAXOPBLOCKS+2)*BSIZE; sz += 471){
    fd = open("bigwrite", O_CREATE | O_RDWR);
    if(fd < 0){
      printf("%s: cannot create bigwrite\n", s);
      exit(1);
    }
    int i;
    for(i = 0; i < 2; i++){
      int cc = write(fd, buf, sz);
      if(cc != sz){
        printf("%s: write(%d) ret %d\n", s, sz, cc);
        exit(1);
      }
    }
    close(fd);
    unlink("bigwrite");
  }
}


// 大文件测试
void
bigfile(char *s)
{
  enum { N = 20, SZ=600 };
  int fd, i, total, cc;

  unlink("bigfile.dat");
  fd = open("bigfile.dat", O_CREATE | O_RDWR);
  if(fd < 0){
    printf("%s: cannot create bigfile", s);
    exit(1);
  }
  for(i = 0; i < N; i++){
    memset(buf, i, SZ);
    if(write(fd, buf, SZ) != SZ){
      printf("%s: write bigfile 失败\n", s);
      exit(1);
    }
  }
  close(fd);

  fd = open("bigfile.dat", 0);
  if(fd < 0){
    printf("%s: cannot open bigfile\n", s);
    exit(1);
  }
  total = 0;
  for(i = 0; ; i++){
    cc = read(fd, buf, SZ/2);
    if(cc < 0){
      printf("%s: read bigfile 失败\n", s);
      exit(1);
    }
    if(cc == 0)
      break;
    if(cc != SZ/2){
      printf("%s: short read bigfile\n", s);
      exit(1);
    }
    if(buf[0] != i/2 || buf[SZ/2-1] != i/2){
      printf("%s: read bigfile wrong data\n", s);
      exit(1);
    }
    total += cc;
  }
  close(fd);
  if(total != N*SZ){
    printf("%s: read bigfile wrong total\n", s);
    exit(1);
  }
  unlink("bigfile.dat");
}

// 文件名长度测试
void
fourteen(char *s)
{
  int fd;

  // DIRSIZ 是 14。

  if(mkdir("12345678901234") != 0){
    printf("%s: mkdir 12345678901234 失败\n", s);
    exit(1);
  }
  if(mkdir("12345678901234/123456789012345") != 0){
    printf("%s: mkdir 12345678901234/123456789012345 失败\n", s);
    exit(1);
  }
  fd = open("123456789012345/123456789012345/123456789012345", O_CREATE);
  if(fd < 0){
    printf("%s: create 123456789012345/123456789012345/123456789012345 失败\n", s);
    exit(1);
  }
  close(fd);
  fd = open("12345678901234/12345678901234/12345678901234", 0);
  if(fd < 0){
    printf("%s: open 12345678901234/12345678901234/12345678901234 失败\n", s);
    exit(1);
  }
  close(fd);

  if(mkdir("12345678901234/12345678901234") == 0){
    printf("%s: mkdir 12345678901234/12345678901234 succeeded!\n", s);
    exit(1);
  }
  if(mkdir("123456789012345/12345678901234") == 0){
    printf("%s: mkdir 12345678901234/123456789012345 succeeded!\n", s);
    exit(1);
  }

  // clean up
  unlink("123456789012345/12345678901234");
  unlink("12345678901234/12345678901234");
  unlink("12345678901234/12345678901234/12345678901234");
  unlink("123456789012345/123456789012345/123456789012345");
  unlink("12345678901234/123456789012345");
  unlink("12345678901234");
}

// 删除 "." 和 ".." 测试
void
rmdot(char *s)
{
  if(mkdir("dots") != 0){
    printf("%s: mkdir dots 失败\n", s);
    exit(1);
  }
  if(chdir("dots") != 0){
    printf("%s: chdir dots 失败\n", s);
    exit(1);
  }
  if(unlink(".") == 0){
    printf("%s: rm . worked!\n", s);
    exit(1);
  }
  if(unlink("..") == 0){
    printf("%s: rm .. worked!\n", s);
    exit(1);
  }
  if(chdir("/") != 0){
    printf("%s: chdir / 失败\n", s);
    exit(1);
  }
  if(unlink("dots/.") == 0){
    printf("%s: unlink dots/. worked!\n", s);
    exit(1);
  }
  if(unlink("dots/..") == 0){
    printf("%s: unlink dots/.. worked!\n", s);
    exit(1);
  }
  if(unlink("dots") != 0){
    printf("%s: unlink dots 失败!\n", s);
    exit(1);
  }
}

// 目录与文件同名测试
void
dirfile(char *s)
{
  int fd;

  fd = open("dirfile", O_CREATE);
  if(fd < 0){
    printf("%s: create dirfile 失败\n", s);
    exit(1);
  }
  close(fd);
  if(chdir("dirfile") == 0){
    printf("%s: chdir dirfile succeeded!\n", s);
    exit(1);
  }
  fd = open("dirfile/xx", 0);
  if(fd >= 0){
    printf("%s: create dirfile/xx succeeded!\n", s);
    exit(1);
  }
  fd = open("dirfile/xx", O_CREATE);
  if(fd >= 0){
    printf("%s: create dirfile/xx succeeded!\n", s);
    exit(1);
  }
  if(mkdir("dirfile/xx") == 0){
    printf("%s: mkdir dirfile/xx succeeded!\n", s);
    exit(1);
  }
  if(unlink("dirfile/xx") == 0){
    printf("%s: unlink dirfile/xx succeeded!\n", s);
    exit(1);
  }
  if(link("README", "dirfile/xx") == 0){
    printf("%s: link to dirfile/xx succeeded!\n", s);
    exit(1);
  }
  if(unlink("dirfile") != 0){
    printf("%s: unlink dirfile 失败!\n", s);
    exit(1);
  }

  fd = open(".", O_RDWR);
  if(fd >= 0){
    printf("%s: open . for writing succeeded!\n", s);
    exit(1);
  }
  fd = open(".", 0);
  if(write(fd, "x", 1) > 0){
    printf("%s: write . succeeded!\n", s);
    exit(1);
  }
  close(fd);
}

// 测试 iput() 是否在 _namei() 的末尾被调用。
// 同时测试空文件名。
void
iref(char *s)
{
  int i, fd;

  for(i = 0; i < NINODE + 1; i++){
    if(mkdir("irefd") != 0){
      printf("%s: mkdir irefd 失败\n", s);
      exit(1);
    }
    if(chdir("irefd") != 0){
      printf("%s: chdir irefd 失败\n", s);
      exit(1);
    }

    mkdir("");
    link("README", "");
    fd = open("", O_CREATE);
    if(fd >= 0)
      close(fd);
    fd = open("xx", O_CREATE);
    if(fd >= 0)
      close(fd);
    unlink("xx");
  }

  // clean up
  for(i = 0; i < NINODE + 1; i++){
    chdir("..");
    unlink("irefd");
  }

  chdir("/");
}

// 测试 fork 失败能否正常处理
// forktest 二进制文件也做这个，但它首先会用完进程条目。
// 在更大的 usertests 二进制文件中，我们首先会用完内存。
void
forktest(char *s)
{
  enum{ N = 1000 };
  int n, pid;

  for(n=0; n<N; n++){
    pid = fork();
    if(pid < 0)
      break;
    if(pid == 0)
      exit(0);
  }

  if (n == 0) {
    printf("%s: no fork at all!\n", s);
    exit(1);
  }

  if(n == N){
    printf("%s: fork claimed to work 1000 times!\n", s);
    exit(1);
  }

  for(; n > 0; n--){
    if(wait(0) < 0){
      printf("%s: wait stopped early\n", s);
      exit(1);
    }
  }

  if(wait(0) != -1){
    printf("%s: wait got too many\n", s);
    exit(1);
  }
}

// sbrk 基本测试
void
sbrkbasic(char *s)
{
  enum { TOOMUCH=1024*1024*1024};
  int i, pid, xstatus;
  char *c, *a, *b;

  // sbrk() 是否返回预期的失败值？
  pid = fork();
  if(pid < 0){
    printf("fork 失败 in sbrkbasic\n");
    exit(1);
  }
  if(pid == 0){
    a = sbrk(TOOMUCH);
    if(a == (char*)0xffffffffffffffffL){
      // 如果这个失败了也没关系。
      exit(0);
    }
    
    for(b = a; b < a+TOOMUCH; b += 4096){
      *b = 99;
    }
    
    // 我们不应该到这里！要么 sbrk(TOOMUCH)
    // 应该失败，要么（使用懒分配）
    // 页面错误应该已经杀死了这个进程。
    exit(1);
  }

  wait(&xstatus);
  if(xstatus == 1){
    printf("%s: too much memory allocated!\n", s);
    exit(1);
  }

  // sbrk() 能否分配小于一页的内存？
  a = sbrk(0);
  for(i = 0; i < 5000; i++){
    b = sbrk(1);
    if(b != a){
      printf("%s: sbrk test 失败 %d %p %p\n", s, i, a, b);
      exit(1);
    }
    *b = 1;
    a = b + 1;
  }
  pid = fork();
  if(pid < 0){
    printf("%s: sbrk test fork 失败\n", s);
    exit(1);
  }
  c = sbrk(1);
  c = sbrk(1);
  if(c != a + 1){
    printf("%s: sbrk test 失败 post-fork\n", s);
    exit(1);
  }
  if(pid == 0)
    exit(0);
  wait(&xstatus);
  exit(xstatus);
}

// 大量 sbrk 测试
void
sbrkmuch(char *s)
{
  enum { BIG=100*1024*1024 };
  char *c, *oldbrk, *a, *lastaddr, *p;
  uint64 amt;

  oldbrk = sbrk(0);

  // 地址空间能否增长到一个很大的值？
  a = sbrk(0);
  amt = BIG - (uint64)a;
  p = sbrk(amt);
  if (p != a) {
    printf("%s: sbrk test 失败 to grow big address space; enough phys mem?\n", s);
    exit(1);
  }

  // 触摸每个页面以确保它存在。
  char *eee = sbrk(0);
  for(char *pp = a; pp < eee; pp += 4096)
    *pp = 1;

  lastaddr = (char*) (BIG-1);
  *lastaddr = 99;

  // 能否取消分配？
  a = sbrk(0);
  c = sbrk(-PGSIZE);
  if(c == (char*)0xffffffffffffffffL){
    printf("%s: sbrk could not deallocate\n", s);
    exit(1);
  }
  c = sbrk(0);
  if(c != a - PGSIZE){
    printf("%s: sbrk deallocation produced wrong address, a %p c %p\n", s, a, c);
    exit(1);
  }

  // 能否重新分配那个页面？
  a = sbrk(0);
  c = sbrk(PGSIZE);
  if(c != a || sbrk(0) != a + PGSIZE){
    printf("%s: sbrk re-allocation 失败, a %p c %p\n", s, a, c);
    exit(1);
  }
  if(*lastaddr == 99){
    // 应该是零
    printf("%s: sbrk de-allocation didn't really deallocate\n", s);
    exit(1);
  }

  a = sbrk(0);
  c = sbrk(-(sbrk(0) - oldbrk));
  if(c != a){
    printf("%s: sbrk downsize 失败, a %p c %p\n", s, a, c);
    exit(1);
  }
}

// 我们能读取内核的内存吗？
void
kernmem(char *s)
{
  char *a;
  int pid;

  for(a = (char*)(KERNBASE); a < (char*) (KERNBASE+2000000); a += 50000){
    pid = fork();
    if(pid < 0){
      printf("%s: fork 失败\n", s);
      exit(1);
    }
    if(pid == 0){
      printf("%s: oops could read %p = %x\n", s, a, *a);
      exit(1);
    }
    int xstatus;
    wait(&xstatus);
    if(xstatus != -1)  // 内核是否杀死了子进程？
      exit(1);
  }
}

// 用户代码不应能写入高于 MAXVA 的地址。
void
MAXVAplus(char *s)
{
  volatile uint64 a = MAXVA;
  for( ; a != 0; a <<= 1){
    int pid;
    pid = fork();
    if(pid < 0){
      printf("%s: fork 失败\n", s);
      exit(1);
    }
    if(pid == 0){
      *(char*)a = 99;
      printf("%s: oops wrote %p\n", s, (void*)a);
      exit(1);
    }
    int xstatus;
    wait(&xstatus);
    if(xstatus != -1)  // 内核是否杀死了子进程？
      exit(1);
  }
}

// 如果我们用尽系统内存，它会清理最后一次失败的分配吗？
void
sbrkfail(char *s)
{
  enum { BIG=100*1024*1024 };
  int i, xstatus;
  int fds[2];
  char scratch;
  char *c, *a;
  int pids[10];
  int pid;
 
  if(pipe(fds) != 0){
    printf("%s: pipe() 失败\n", s);
    exit(1);
  }
  for(i = 0; i < sizeof(pids)/sizeof(pids[0]); i++){
    if((pids[i] = fork()) == 0){
      // 分配大量内存
      sbrk(BIG - (uint64)sbrk(0));
      write(fds[1], "x", 1);
      // 一直等待直到被杀死
      for(;;) sleep(1000);
    }
    if(pids[i] != -1)
      read(fds[0], &scratch, 1);
  }

  // 如果那些失败的分配释放了它们确实分配的页面，
  // 我们将能够在这里分配
  c = sbrk(PGSIZE);
  for(i = 0; i < sizeof(pids)/sizeof(pids[0]); i++){
    if(pids[i] == -1)
      continue;
    kill(pids[i]);
    wait(0);
  }
  if(c == (char*)0xffffffffffffffffL){
    printf("%s: 失败 sbrk leaked memory\n", s);
    exit(1);
  }

  // 测试在上述分配的页面上运行 fork
  pid = fork();
  if(pid < 0){
    printf("%s: fork 失败\n", s);
    exit(1);
  }
  if(pid == 0){
    // 分配大量内存。
    // 这应该会产生一个页面错误，
    // 因此不会完成。
    a = sbrk(0);
    sbrk(10*BIG);
    int n = 0;
    for (i = 0; i < 10*BIG; i += PGSIZE) {
      n += *(a+i);
    }
    // 打印 n 以便编译器不会优化掉 for 循环。
    printf("%s: allocate a lot of memory succeeded %d\n", s, n);
    exit(1);
  }
  wait(&xstatus);
  if(xstatus != -1 && xstatus != 2)
    exit(1);
}

  
// 测试从/向已分配内存的读/写
void
sbrkarg(char *s)
{
  char *a;
  int fd, n;

  a = sbrk(PGSIZE);
  fd = open("sbrk", O_CREATE|O_WRONLY);
  unlink("sbrk");
  if(fd < 0)  {
    printf("%s: open sbrk 失败\n", s);
    exit(1);
  }
  if ((n = write(fd, a, PGSIZE)) < 0) {
    printf("%s: write sbrk 失败\n", s);
    exit(1);
  }
  close(fd);

  // 测试写入已分配内存
  a = sbrk(PGSIZE);
  if(pipe((int *) a) != 0){
    printf("%s: pipe() 失败\n", s);
    exit(1);
  } 
}

// 验证测试
void
validatetest(char *s)
{
  int hi;
  uint64 p;

  hi = 1100*1024;
  for(p = 0; p <= (uint)hi; p += PGSIZE){
    // 尝试通过传入一个坏的字符串指针来使内核崩溃
    if(link("nosuchfile", (char*)p) != -1){
      printf("%s: link should not succeed\n", s);
      exit(1);
    }
  }
}

// 未初始化的数据是否以零开始？
char uninit[10000];
void
bsstest(char *s)
{
  int i;

  for(i = 0; i < sizeof(uninit); i++){
    if(uninit[i] != '\0'){
      printf("%s: bss test 失败\n", s);
      exit(1);
    }
  }
}

// 如果参数大于一页，exec 是否返回错误？
// 还是它会写在栈下面，破坏指令/数据？
void
bigargtest(char *s)
{
  int pid, fd, xstatus;

  unlink("bigarg-ok");
  pid = fork();
  if(pid == 0){
    static char *args[MAXARG];
    int i;
    char big[400];
    memset(big, ' ', sizeof(big));
    big[sizeof(big)-1] = '\0';
    for(i = 0; i < MAXARG-1; i++)
      args[i] = big;
    args[MAXARG-1] = 0;
    // 这个 exec() 应该失败（并返回），因为参数太大了。
    exec("echo", args);
    fd = open("bigarg-ok", O_CREATE);
    close(fd);
    exit(0);
  } else if(pid < 0){
    printf("%s: bigargtest: fork 失败\n", s);
    exit(1);
  }
  
  wait(&xstatus);
  if(xstatus != 0)
    exit(xstatus);
  fd = open("bigarg-ok", 0);
  if(fd < 0){
    printf("%s: bigarg test 失败!\n", s);
    exit(1);
  }
  close(fd);
}

// 当文件系统用完块时会发生什么？
// 答案：balloc panic，所以这个测试没用。
void
fsfull()
{
  int nfiles;
  int fsblocks = 0;

  printf("fsfull test\n");

  for(nfiles = 0; ; nfiles++){
    char name[64];
    name[0] = 'f';
    name[1] = '0' + nfiles / 1000;
    name[2] = '0' + (nfiles % 1000) / 100;
    name[3] = '0' + (nfiles % 100) / 10;
    name[4] = '0' + (nfiles % 10);
    name[5] = '\0';
    printf("writing %s\n", name);
    int fd = open(name, O_CREATE|O_RDWR);
    if(fd < 0){
      printf("open %s 失败\n", name);
      break;
    }
    int total = 0;
    while(1){
      int cc = write(fd, buf, BSIZE);
      if(cc < BSIZE)
        break;
      total += cc;
      fsblocks++;
    }
    printf("wrote %d bytes\n", total);
    close(fd);
    if(total == 0)
      break;
  }

  while(nfiles >= 0){
    char name[64];
    name[0] = 'f';
    name[1] = '0' + nfiles / 1000;
    name[2] = '0' + (nfiles % 1000) / 100;
    name[3] = '0' + (nfiles % 100) / 10;
    name[4] = '0' + (nfiles % 10);
    name[5] = '\0';
    unlink(name);
    nfiles--;
  }

  printf("fsfull test finished\n");
}

// 参数指针测试
void argptest(char *s)
{
  int fd;
  fd = open("init", O_RDONLY);
  if (fd < 0) {
    printf("%s: open 失败\n", s);
    exit(1);
  }
  read(fd, sbrk(0) - 1, -1);
  close(fd);
}

// 检查用户栈下面是否有一个无效页面，
// 以捕获堆栈溢出。
void
stacktest(char *s)
{
  int pid;
  int xstatus;
  
  pid = fork();
  if(pid == 0) {
    char *sp = (char *) r_sp();
    sp -= USERSTACK*PGSIZE;
    // *sp 应该导致一个陷阱。
    printf("%s: stacktest: read below stack %d\n", s, *sp);
    exit(1);
  } else if(pid < 0){
    printf("%s: fork 失败\n", s);
    exit(1);
  }
  wait(&xstatus);
  if(xstatus == -1)  // 内核杀死了子进程？
    exit(0);
  else
    exit(xstatus);
}

// 检查对一些禁止地址的写入是否会导致故障，
// 例如进程的文本段和 TRAMPOLINE。
void
nowrite(char *s)
{
  int pid;
  int xstatus;
  uint64 addrs[] = { 0, 0x80000000LL, 0x3fffffe000, 0x3ffffff000, 0x4000000000,
                     0xffffffffffffffff };
  
  for(int ai = 0; ai < sizeof(addrs)/sizeof(addrs[0]); ai++){
    pid = fork();
    if(pid == 0) {
      volatile int *addr = (int *) addrs[ai];
      *addr = 10;
      printf("%s: write to %p did not fail!\n", s, addr);
      exit(0);
    } else if(pid < 0){
      printf("%s: fork 失败\n", s);
      exit(1);
    }
    wait(&xstatus);
    if(xstatus == 0){
      // 内核没有杀死子进程！
      exit(1);
    }
  }
  exit(0);
}

// 回归测试。copyin()、copyout() 和 copyinstr() 过去将
// 虚拟页面地址转换为 uint，这（在某些疯狂的系统调用参数下）
// 导致内核页面错误。
void *big = (void*) 0xeaeb0b5b00002f5e;
void
pgbug(char *s)
{
  char *argv[1];
  argv[0] = 0;
  exec(big, argv);
  pipe(big);

  exit(0);
}

// 回归测试。如果一个进程 sbrk() 其大小
// 小于一页，或为零，或者减少 break 的量太小
// 不足以导致页面被释放，内核会恐慌吗？
void
sbrkbugs(char *s)
{
  int pid = fork();
  if(pid < 0){
    printf("fork 失败\n");
    exit(1);
  }
  if(pid == 0){
    int sz = (uint64) sbrk(0);
    // 释放所有用户内存；过去有一个 bug
    // 在这种情况下不会正确调整 p->sz，
    // 导致 exit() 恐慌。
    sbrk(-sz);
    // 这里用户页面错误。
    exit(0);
  }
  wait(0);

  pid = fork();
  if(pid < 0){
    printf("fork 失败\n");
    exit(1);
  }
  if(pid == 0){
    int sz = (uint64) sbrk(0);
    // 将 break 设置到第一个页面的某个位置；
    // 过去有一个 bug 会错误地释放第一个页面。
    sbrk(-(sz - 3500));
    exit(0);
  }
  wait(0);

  pid = fork();
  if(pid < 0){
    printf("fork 失败\n");
    exit(1);
  }
  if(pid == 0){
    // 将 break 设置在页面的中间。
    sbrk((10*4096 + 2048) - (uint64)sbrk(0));

    // 稍微减少 break，但不足以
    // 导致页面被释放。这过去会导致恐慌。
    sbrk(-10);

    exit(0);
  }
  wait(0);

  exit(0);
}

// 如果进程大小略大于页面边界，然后
// 缩小到略小于该页面边界，内核是否
// 仍然可以从最后一页的地址 copyin()？
void
sbrklast(char *s)
{
  uint64 top = (uint64) sbrk(0);
  if((top % 4096) != 0)
    sbrk(4096 - (top % 4096));
  sbrk(4096);
  sbrk(10);
  sbrk(-20);
  top = (uint64) sbrk(0);
  char *p = (char *) (top - 64);
  p[0] = 'x';
  p[1] = '\0';
  int fd = open(p, O_RDWR|O_CREATE);
  write(fd, p, 1);
  close(fd);
  fd = open(p, O_RDWR);
  p[0] = '\0';
  read(fd, p, 1);
  if(p[0] != 'x')
    exit(1);
}

// sbrk 是否处理带负参数的 signed int32 环绕？
void
sbrk8000(char *s)
{
  sbrk(0x80000004);
  volatile char *top = sbrk(0);
  *(top-1) = *(top-1) + 1;
}



// 回归测试。测试如果其中一个参数无效，exec() 是否泄漏内存。
// 如果内核不恐慌，则测试通过。
void
badarg(char *s)
{
  for(int i = 0; i < 50000; i++){
    char *argv[2];
    argv[0] = (char*)0xffffffff;
    argv[1] = 0;
    exec("echo", argv);
  }
  
  exit(0);
}

// 快速测试列表
struct test {
  void (*f)(char *);
  char *s;
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

  { 0, 0},
};

//
// 耗时较长的测试部分
//

// 使用间接块的目录
void
bigdir(char *s)
{
  enum { N = 500 };
  int i, fd;
  char name[10];

  unlink("bd");

  fd = open("bd", O_CREATE);
  if(fd < 0){
    printf("%s: bigdir create 失败\n", s);
    exit(1);
  }
  close(fd);

  for(i = 0; i < N; i++){
    name[0] = 'x';
    name[1] = '0' + (i / 64);
    name[2] = '0' + (i % 64);
    name[3] = '\0';
    if(link("bd", name) != 0){
      printf("%s: bigdir i=%d link(bd, %s) 失败\n", s, i, name);
      exit(1);
    }
  }

  unlink("bd");
  for(i = 0; i < N; i++){
    name[0] = 'x';
    name[1] = '0' + (i / 64);
    name[2] = '0' + (i % 64);
    name[3] = '\0';
    if(unlink(name) != 0){
      printf("%s: bigdir unlink 失败", s);
      exit(1);
    }
  }
}

// 并发写入以尝试在 virtio 磁盘驱动程序中引发死锁。
void
manywrites(char *s)
{
  int nchildren = 4;
  int howmany = 30; // 增加以查找死锁
  
  for(int ci = 0; ci < nchildren; ci++){
    int pid = fork();
    if(pid < 0){
      printf("fork 失败\n");
      exit(1);
    }

    if(pid == 0){
      char name[3];
      name[0] = 'b';
      name[1] = 'a' + ci;
      name[2] = '\0';
      unlink(name);
      
      for(int iters = 0; iters < howmany; iters++){
        for(int i = 0; i < ci+1; i++){
          int fd = open(name, O_CREATE | O_RDWR);
          if(fd < 0){
            printf("%s: cannot create %s\n", s, name);
            exit(1);
          }
          int sz = sizeof(buf);
          int cc = write(fd, buf, sz);
          if(cc != sz){
            printf("%s: write(%d) ret %d\n", s, sz, cc);
            exit(1);
          }
          close(fd);
        }
        unlink(name);
      }

      unlink(name);
      exit(0);
    }
  }

  for(int ci = 0; ci < nchildren; ci++){
    int st = 0;
    wait(&st);
    if(st != 0)
      exit(st);
  }
  exit(0);
}

// 回归测试。带有无效缓冲区指针的 write() 是否会导致
// 为文件分配一个块，然后在文件被删除时该块未被释放？
// 如果内核存在此 bug，它将 panic: balloc: out of blocks。
// assumed_free 可能需要提高到大于空闲块的数量。
// 此测试需要很长时间。
void
badwrite(char *s)
{
  int assumed_free = 600;
  
  unlink("junk");
  for(int i = 0; i < assumed_free; i++){
    int fd = open("junk", O_CREATE|O_WRONLY);
    if(fd < 0){
      printf("open junk 失败\n");
      exit(1);
    }
    write(fd, (char*)0xffffffffffL, 1);
    close(fd);
    unlink("junk");
  }

  int fd = open("junk", O_CREATE|O_WRONLY);
  if(fd < 0){
    printf("open junk 失败\n");
    exit(1);
  }
  if(write(fd, "x", 1) != 1){
    printf("write 失败\n");
    exit(1);
  }
  close(fd);
  unlink("junk");

  exit(0);
}

// 测试 exec() 代码在内存不足时进行清理。
// 这实际上是测试这种情况不会导致 panic。
void
execout(char *s)
{
  for(int avail = 0; avail < 15; avail++){
    int pid = fork();
    if(pid < 0){
      printf("fork 失败\n");
      exit(1);
    } else if(pid == 0){
      // 分配所有内存。
      while(1){
        uint64 a = (uint64) sbrk(4096);
        if(a == 0xffffffffffffffffLL)
          break;
        *(char*)(a + 4096 - 1) = 1;
      }

      // 释放几个页面，以便让 exec() 取得一些进展。
      for(int i = 0; i < avail; i++)
        sbrk(-4096);
      
      close(1);
      char *args[] = { "echo", "x", 0 };
      exec("echo", args);
      exit(0);
    } else {
      wait((int*)0);
    }
  }

  exit(0);
}

// 内核能容忍磁盘空间用完吗？
void
diskfull(char *s)
{
  int fi;
  int done = 0;

  unlink("diskfulldir");
  
  for(fi = 0; done == 0 && '0' + fi < 0177; fi++){
    char name[32];
    name[0] = 'b';
    name[1] = 'i';
    name[2] = 'g';
    name[3] = '0' + fi;
    name[4] = '\0';
    unlink(name);
    int fd = open(name, O_CREATE|O_RDWR|O_TRUNC);
    if(fd < 0){
      // 糟糕，在用完块之前用完了 inode。
      printf("%s: could not create file %s\n", s, name);
      done = 1;
      break;
    }
    for(int i = 0; i < MAXFILE; i++){
      char buf[BSIZE];
      if(write(fd, buf, BSIZE) != BSIZE){
        done = 1;
        close(fd);
        break;
      }
    }
    close(fd);
  }

  // 现在没有空闲块了，测试 dirlink()
  // 在无法扩展目录内容时仅失败（不 panic）。
  // 预期其中一个文件创建会失败。
  int nzz = 128;
  for(int i = 0; i < nzz; i++){
    char name[32];
    name[0] = 'z';
    name[1] = 'z';
    name[2] = '0' + (i / 32);
    name[3] = '0' + (i % 32);
    name[4] = '\0';
    unlink(name);
    int fd = open(name, O_CREATE|O_RDWR|O_TRUNC);
    if(fd < 0)
      break;
    close(fd);
  }

  // 这个 mkdir() 预期会失败。
  if(mkdir("diskfulldir") == 0)
    printf("%s: mkdir(diskfulldir) unexpectedly succeeded!\n", s);

  unlink("diskfulldir");

  for(int i = 0; i < nzz; i++){
    char name[32];
    name[0] = 'z';
    name[1] = 'z';
    name[2] = '0' + (i / 32);
    name[3] = '0' + (i % 32);
    name[4] = '\0';
    unlink(name);
  }

  for(int i = 0; '0' + i < 0177; i++){
    char name[32];
    name[0] = 'b';
    name[1] = 'i';
    name[2] = 'g';
    name[3] = '0' + i;
    name[4] = '\0';
    unlink(name);
  }
}

// inode 用尽测试
void
outofinodes(char *s)
{
  int nzz = 32*32;
  for(int i = 0; i < nzz; i++){
    char name[32];
    name[0] = 'z';
    name[1] = 'z';
    name[2] = '0' + (i / 32);
    name[3] = '0' + (i % 32);
    name[4] = '\0';
    unlink(name);
    int fd = open(name, O_CREATE|O_RDWR|O_TRUNC);
    if(fd < 0){
      // 最终预期会失败。
      break;
    }
    close(fd);
  }

  for(int i = 0; i < nzz; i++){
    char name[32];
    name[0] = 'z';
    name[1] = 'z';
    name[2] = '0' + (i / 32);
    name[3] = '0' + (i % 32);
    name[4] = '\0';
    unlink(name);
  }
}

// 慢速测试列表
struct test slowtests[] = {
  {bigdir, "bigdir"},
  {manywrites, "manywrites"},
  {badwrite, "badwrite" },
  {execout, "execout"},
  {diskfull, "diskfull"},
  {outofinodes, "outofinodes"},
    
  { 0, 0},
};

//
// 驱动测试
//

// 在其自己的进程中运行每个测试。如果子进程的 exit()
// 表示成功，则 run 返回 1。
int
run(void f(char *), char *s) {
  int pid;
  int xstatus;

  printf("test %s: ", s);
  if((pid = fork()) < 0) {
    printf("runtest: fork error\n");
    exit(1);
  }
  if(pid == 0) {
    f(s);
    exit(0);
  } else {
    wait(&xstatus);
    if(xstatus != 0) 
      printf("失败\n");
    else
      printf("OK\n");
    return xstatus == 0;
  }
}

// 运行测试集
int
runtests(struct test *tests, char *justone, int continuous) {
  for (struct test *t = tests; t->s != 0; t++) {
    if((justone == 0) || strcmp(t->s, justone) == 0) {
      if(!run(t->f, t->s)){
        if(continuous != 2){
          printf("SOME TESTS 失败\n");
          return 1;
        }
      }
    }
  }
  return 0;
}


//
// 使用 sbrk() 来计算有多少空闲物理内存页。
// 触摸页面以强制分配。
// 因为内存不足时懒分配会导致进程
// 发生故障并被杀死，所以 fork 并报告回来。
//
int
countfree()
{
  int fds[2];

  if(pipe(fds) < 0){
    printf("pipe() 失败 in countfree()\n");
    exit(1);
  }
  
  int pid = fork();

  if(pid < 0){
    printf("fork 失败 in countfree()\n");
    exit(1);
  }

  if(pid == 0){
    close(fds[0]);
    
    while(1){
      uint64 a = (uint64) sbrk(4096);
      if(a == 0xffffffffffffffff){
        break;
      }

      // 修改内存以确保它真的被分配了。
      *(char *)(a + 4096 - 1) = 1;

      // 报告多一页。
      if(write(fds[1], "x", 1) != 1){
        printf("write() 失败 in countfree()\n");
        exit(1);
      }
    }

    exit(0);
  }

  close(fds[1]);

  int n = 0;
  while(1){
    char c;
    int cc = read(fds[0], &c, 1);
    if(cc < 0){
      printf("read() 失败 in countfree()\n");
      exit(1);
    }
    if(cc == 0)
      break;
    n += 1;
  }

  close(fds[0]);
  wait((int*)0);
  
  return n;
}

// 主测试驱动程序
int
drivetests(int quick, int continuous, char *justone) {
  do {
    printf("usertests starting\n");
    int free0 = countfree();
    int free1 = 0;
    if (runtests(quicktests, justone, continuous)) {
      if(continuous != 2) {
        return 1;
      }
    }
    if(!quick) {
      if (justone == 0)
        printf("usertests slow tests starting\n");
      if (runtests(slowtests, justone, continuous)) {
        if(continuous != 2) {
          return 1;
        }
      }
    }
    if((free1 = countfree()) < free0) {
      printf("失败 -- lost some free pages %d (out of %d)\n", free1, free0);
      if(continuous != 2) {
        return 1;
      }
    }
  } while(continuous);
  return 0;
}

// 主函数
int
main(int argc, char *argv[])
{
  int continuous = 0;
  int quick = 0;
  char *justone = 0;

  if(argc == 2 && strcmp(argv[1], "-q") == 0){
    quick = 1;
  } else if(argc == 2 && strcmp(argv[1], "-c") == 0){
    continuous = 1;
  } else if(argc == 2 && strcmp(argv[1], "-C") == 0){
    continuous = 2;
  } else if(argc == 2 && argv[1][0] != '-'){
    justone = argv[1];
  } else if(argc > 1){
    printf("用法: usertests [-c] [-C] [-q] [testname]\n");
    exit(1);
  }
  if (drivetests(quick, continuous, justone)) {
    exit(1);
  }
  printf("ALL TESTS PASSED\n");
  exit(0);
}
