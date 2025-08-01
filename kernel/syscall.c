// 系统调用相关的实现。
//
// xv6-riscv 内核通过 `ecall` 指令提供系统调用服务。
// `ecall` 指令会进入内核态，并跳转到 `kernelvec` 处的代码。
// `kernelvec` 会保存用户寄存器，并调用 `trap` 函数。
// `trap` 函数会判断陷阱类型，如果是系统调用，则会调用 `syscall` 函数。
// `syscall` 函数会根据系统调用号，在 `syscalls` 表中查找并执行相应的处理函数。
// 处理函数执行完毕后，返回值会存入当前进程陷阱帧的 a0 寄存器。
// 最后，通过 `sret` 指令返回用户态，用户程序从 a0 寄存器中获取返回值。

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "proc.h"
#include "syscall.h"
#include "defs.h"

// 从用户空间获取一个64位整数。
// 用户虚拟地址 `addr` 处的数据将被拷贝到内核变量 `*ip` 中。
//
// 成功时返回 0，失败时返回 -1。
int
fetchaddr(uint64 addr, uint64 *ip)
{
  struct proc *p = myproc();
  // 检查地址是否在当前进程的地址空间范围内。
  // 必须同时检查 `addr` 和 `addr + sizeof(uint64)`，以防止地址溢出。
  if(addr >= p->sz || addr+sizeof(uint64) > p->sz)
    return -1;
  // 使用 `copyin` 函数安全地从用户空间拷贝数据到内核空间。
  if(copyin(p->pagetable, (char *)ip, addr, sizeof(*ip)) != 0)
    return -1;
  return 0;
}

// 从用户空间获取一个以 null 结尾的字符串。
// 用户虚拟地址 `addr` 处的字符串将被拷贝到内核缓冲区 `buf` 中，最多 `max` 字节。
//
// 成功时返回字符串的长度（不包括结尾的 null 字符），失败时返回 -1。
int
fetchstr(uint64 addr, char *buf, int max)
{
  struct proc *p = myproc();
  // 使用 `copyinstr` 函数安全地从用户空间拷贝字符串到内核空间。
  if(copyinstr(p->pagetable, buf, addr, max) < 0)
    return -1;
  return strlen(buf);
}

// 从陷阱帧中获取第 n 个原始系统调用参数（存储在 a0-a5 寄存器中）。
// RISC-V 架构规定，系统调用的前 6 个参数通过 a0-a5 寄存器传递。
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
  // 如果 n 不在 0-5 范围内，说明代码逻辑有误，触发 panic。
  panic("argraw");
  return -1;
}

// 获取第 n 个 32 位整型系统调用参数，并将其存入 *ip。
void
argint(int n, int *ip)
{
  *ip = argraw(n);
}

// 获取第 n 个指针类型的系统调用参数，并将其存入 *ip。
// 这个函数本身不检查地址的合法性。
// 地址的合法性检查会由后续的 `copyin` 或 `copyout` 等函数完成。
void
argaddr(int n, uint64 *ip)
{
  *ip = argraw(n);
}

// 获取第 n 个字符串类型的系统调用参数。
// 先通过 `argaddr` 获取字符串在用户空间的地址，
// 然后通过 `fetchstr` 将字符串从用户空间拷贝到内核缓冲区 `buf` 中。
//
// 成功时返回字符串长度，失败时返回 -1。
int
argstr(int n, char *buf, int max)
{
  uint64 addr;
  argaddr(n, &addr);
  return fetchstr(addr, buf, max);
}

// 声明所有系统调用处理函数的原型。
// 这些函数的具体实现在 `sysproc.c` 和 `sysfile.c` 文件中。
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
extern uint64 sys_shutdown(void);
extern uint64 sys_reboot(void);

// 系统调用处理函数指针数组。
// `syscall.h` 中定义的系统调用号 (例如 `SYS_fork`) 作为该数组的索引，
// 映射到对应的处理函数 (例如 `sys_fork`)。
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
[SYS_shutdown] sys_shutdown,
[SYS_reboot]   sys_reboot,
};

// 系统调用分发函数。
// 当用户程序执行 `ecall` 指令引发陷阱时，此函数被调用。
void
syscall(void)
{
  int num;
  struct proc *p = myproc();

  // RISC-V 约定，系统调用号存储在 a7 寄存器中。
  // 从当前进程的陷阱帧中读取 a7 寄存器的值。
  num = p->trapframe->a7;
  
  // 检查系统调用号是否合法，并且对应的处理函数是否存在。
  if(num > 0 && num < NELEM(syscalls) && syscalls[num]) {
    // 调用对应的系统调用处理函数。
    // 将处理函数的返回值存入 a0 寄存器，以便返回给用户程序。
    p->trapframe->a0 = syscalls[num]();
  } else {
    // 如果系统调用号无效，则打印错误信息，并返回 -1。
    printf("%d %s: unknown sys call %d\n",
            p->pid, p->name, num);
    p->trapframe->a0 = -1;
  }
}
