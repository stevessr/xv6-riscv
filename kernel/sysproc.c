#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "proc.h"

// sys_exit 系统调用：终止当前进程。
uint64
sys_exit(void)
{
  int n;
  argint(0, &n); // 获取第0个参数（退出状态码）
  exit(n);
  return 0;  // 不会执行到这里
}

// sys_getpid 系统调用：获取当前进程的ID。
uint64
sys_getpid(void)
{
  return myproc()->pid;
}

// sys_fork 系统调用：创建一个子进程。
uint64
sys_fork(void)
{
  return fork();
}

// sys_wait 系统调用：等待一个子进程退出。
uint64
sys_wait(void)
{
  uint64 p;
  argaddr(0, &p); // 获取第0个参数（用于存储子进程退出状态的地址）
  return wait(p);
}

// sys_sbrk 系统调用：增加或减少进程的数据段大小。
uint64
sys_sbrk(void)
{
  uint64 addr;
  int n;

  argint(0, &n); // 获取第0个参数（增加或减少的字节数）
  addr = myproc()->sz;
  if(growproc(n) < 0) // 调用 growproc 来调整内存
    return -1;
  return addr; // 返回旧的数据段末尾地址
}

// sys_sleep 系统调用：使进程休眠指定的 tick 数。
uint64
sys_sleep(void)
{
  int n;
  uint ticks0;

  argint(0, &n); // 获取第0个参数（休眠的 tick 数）
  if(n < 0)
    n = 0;
  acquire(&tickslock);
  ticks0 = ticks;
  // 循环直到经过了 n 个 tick
  while(ticks - ticks0 < n){
    if(killed(myproc())){ // 如果进程被杀死，则提前返回
      release(&tickslock);
      return -1;
    }
    sleep(&ticks, &tickslock); // 在 ticks 通道上休眠
  }
  release(&tickslock);
  return 0;
}

// sys_kill 系统调用：终止指定 PID 的进程。
uint64
sys_kill(void)
{
  int pid;

  argint(0, &pid); // 获取第0个参数（要杀死的进程ID）
  return kill(pid);
}

// sys_uptime 系统调用：返回系统启动后经过的时钟中断次数。
uint64
sys_uptime(void)
{
  uint xticks;

  acquire(&tickslock);
  xticks = ticks;
  release(&tickslock);
  return xticks;
}
