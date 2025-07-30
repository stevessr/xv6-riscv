#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "proc.h"
#include "syscall.h"
#include "defs.h"

// 从当前进程的用户空间地址 addr 处获取一个 uint64。
// 成功时，将值存入 *ip 并返回 0。失败返回 -1。
int
fetchaddr(uint64 addr, uint64 *ip)
{
  struct proc *p = myproc();
  // 检查地址是否在进程的地址空间内。
  // 两个检查都是必需的，以防地址溢出。
  if(addr >= p->sz || addr+sizeof(uint64) > p->sz)
    return -1;
  // 从用户空间拷贝数据到内核空间。
  if(copyin(p->pagetable, (char *)ip, addr, sizeof(*ip)) != 0)
    return -1;
  return 0;
}

// 从当前进程的用户空间地址 addr 处获取一个以 nul 结尾的字符串。
// 成功时返回字符串长度（不包括nul），失败返回 -1。
int
fetchstr(uint64 addr, char *buf, int max)
{
  struct proc *p = myproc();
  if(copyinstr(p->pagetable, buf, addr, max) < 0)
    return -1;
  return strlen(buf);
}

// 从陷阱帧中获取原始的第 n 个系统调用参数（a0-a5）。
static uint64
argraw(int n)
{
  struct proc *p = myproc();
  switch (n) {
  case 0:
    return p->trapframe->a0;
  case 1:
    return p->trapframe->a1;
  case 2:
    return p->trapframe->a2;
  case 3:
    return p->trapframe->a3;
  case 4:
    return p->trapframe->a4;
  case 5:
    return p->trapframe->a5;
  }
  panic("argraw");
  return -1;
}

// 获取第 n 个 32 位系统调用参数，并将其存入 *ip。
void
argint(int n, int *ip)
{
  *ip = argraw(n);
}

// 获取一个指针类型的参数。
// 这里不检查地址的合法性，因为
// 之后的 copyin/copyout 会进行检查。
void
argaddr(int n, uint64 *ip)
{
  *ip = argraw(n);
}

// 获取第 n 个系统调用参数，该参数是一个以 null 结尾的字符串。
// 将字符串复制到 buf 中，最多 max 个字节。
// 成功时返回字符串长度，失败返回 -1。
int
argstr(int n, char *buf, int max)
{
  uint64 addr;
  argaddr(n, &addr);
  return fetchstr(addr, buf, max);
}

// 系统调用处理函数的原型声明。
// 这些函数定义在 sysproc.c 和 sysfile.c 中。
extern uint64 sys_fork(void);
extern uint64 sys_exit(void);
extern uint64 sys_wait(void);
extern uint64 sys_pipe(void);
extern uint64 sys_read(void);
extern uint64 sys_kill(void);
extern uint64 sys_exec(void);
extern uint64 sys_fstat(void);
extern uint64 sys_chdir(void);
extern uint64 sys_dup(void);
extern uint64 sys_getpid(void);
extern uint64 sys_sbrk(void);
extern uint64 sys_sleep(void);
extern uint64 sys_uptime(void);
extern uint64 sys_open(void);
extern uint64 sys_write(void);
extern uint64 sys_mknod(void);
extern uint64 sys_unlink(void);
extern uint64 sys_link(void);
extern uint64 sys_mkdir(void);
extern uint64 sys_close(void);

// 一个函数指针数组，将 syscall.h 中的系统调用号
// 映射到对应的处理函数。
static uint64 (*syscalls[])(void) = {
[SYS_fork]    sys_fork,
[SYS_exit]    sys_exit,
[SYS_wait]    sys_wait,
[SYS_pipe]    sys_pipe,
[SYS_read]    sys_read,
[SYS_kill]    sys_kill,
[SYS_exec]    sys_exec,
[SYS_fstat]   sys_fstat,
[SYS_chdir]   sys_chdir,
[SYS_dup]     sys_dup,
[SYS_getpid]  sys_getpid,
[SYS_sbrk]    sys_sbrk,
[SYS_sleep]   sys_sleep,
[SYS_uptime]  sys_uptime,
[SYS_open]    sys_open,
[SYS_write]   sys_write,
[SYS_mknod]   sys_mknod,
[SYS_unlink]  sys_unlink,
[SYS_link]    sys_link,
[SYS_mkdir]   sys_mkdir,
[SYS_close]   sys_close,
};

// 系统调用分发函数。
// 当用户程序执行 `ecall` 指令时，会陷入内核态，最终调用此函数。
void
syscall(void)
{
  int num;
  struct proc *p = myproc();

  // 从陷阱帧的 a7 寄存器获取系统调用号。
  num = p->trapframe->a7;
  if(num > 0 && num < NELEM(syscalls) && syscalls[num]) {
    // 使用 num 作为索引，查找并调用相应的系统调用处理函数。
    // 将返回值存储在 a0 寄存器中，以便返回给用户程序。
    p->trapframe->a0 = syscalls[num]();
  } else {
    printf("%d %s: unknown sys call %d\n",
            p->pid, p->name, num);
    p->trapframe->a0 = -1;
  }
}
