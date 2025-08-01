#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "proc.h"

// sys_exit 系统调用：终止当前进程。
//
// exit() 系统调用会使当前进程停止执行并释放资源。
// 它接受一个整数参数作为退出状态，父进程可以通过 wait() 来获取这个状态。
uint64
sys_exit(void)
{
  int n;
  // 从用户空间获取第一个整数参数，即退出状态码。
  argint(0, &n);
  exit(n);
  return 0;  // exit() 不会返回，所以这行代码永远不会执行到。
}

// sys_getpid 系统调用：获取当前进程的ID。
//
// getpid() 系统调用返回当前进程的唯一标识符（Process ID）。
uint64
sys_getpid(void)
{
  return myproc()->pid;
}

// sys_fork 系统调用：创建一个子进程。
//
// fork() 系统调用会创建一个新的进程，这个新进程是调用进程的副本。
// 在父进程中，fork() 返回新创建子进程的 PID。
// 在子进程中，fork() 返回 0。
// 如果出现错误，返回-1。
uint64
sys_fork(void)
{
  return fork();
}

// sys_wait 系统调用：等待一个子进程退出，并获取其退出状态。
//
// wait() 系统调用会暂停当前进程的执行，直到它的一个子进程退出。
// 它会返回退出子进程的 PID。
// 子进程的退出状态码会被写入参数 p 指向的地址。
uint64
sys_wait(void)
{
  uint64 p;
  // 从用户空间获取第一个参数，它是一个指向用于存储退出状态码的地址。
  argaddr(0, &p);
  return wait(p);
}

// sys_sbrk 系统调用：增加或减少进程的数据段大小。
//
// sbrk() 通过指定的字节数 n 来增长或收缩进程的内存。
// n 可以是正数（增长）或负数（收缩）。
// 成功时，返回旧的数据段末尾地址。
// 失败时（例如，内存不足），返回 -1。
uint64
sys_sbrk(void)
{
  uint64 addr;
  int n;

  // 从用户空间获取第一个整数参数，即希望增加或减少的字节数。
  argint(0, &n);
  addr = myproc()->sz;
  if(growproc(n) < 0) // 调用 growproc 来实际调整进程大小。
    return -1;
  return addr; // 返回调整前的数据段末尾地址。
}

// sys_sleep 系统调用：使进程休眠指定的 tick 数。
//
// sleep() 系统调用使当前进程暂停执行 n 个时钟滴答（ticks）。
// 一个时钟滴答是 xv6 中的一个时间单位。
uint64
sys_sleep(void)
{
  int n;
  uint ticks0;

  // 从用户空间获取第一个整数参数，即休眠的时钟滴答数。
  argint(0, &n);
  if(n < 0)
    n = 0;
  
  // 获取时钟锁，以原子方式读取当前的 ticks 值。
  acquire(&tickslock);
  ticks0 = ticks;
  // 循环直到已经过去的滴答数达到 n。
  while(ticks - ticks0 < n){
    // 如果在休眠期间进程被杀死，则立即中止休眠并返回。
    if(killed(myproc())){
      release(&tickslock);
      return -1;
    }
    // 调用 sleep 函数，将进程置于休眠状态，等待时钟中断唤醒。
    // sleep 会原子地释放 tickslock 并等待。
    sleep(&ticks, &tickslock);
  }
  // 休眠时间到，释放时钟锁。
  release(&tickslock);
  return 0;
}

// sys_kill 系统调用：终止指定 PID 的进程。
//
// kill() 系统调用向指定 PID 的进程发送一个“杀死”信号。
// 这会将目标进程的 killed 标志位置位。
// 当目标进程下次进入内核时（例如，通过系统调用或时钟中断），
// 它会检查这个标志并自行退出。
uint64
sys_kill(void)
{
  int pid;

  // 从用户空间获取第一个整数参数，即要杀死的进程ID。
  argint(0, &pid);
  return kill(pid);
}

// sys_uptime 系统调用：返回系统启动后经过的时钟中断次数。
//
// uptime() 返回自系统启动以来全局变量 ticks 的值。
// ticks 在每次时钟中断时递增，因此它代表了系统的运行时间。
uint64
sys_uptime(void)
{
  uint xticks;

  // 获取时钟锁以确保对 ticks 的原子读取。
  acquire(&tickslock);
  xticks = ticks;
  release(&tickslock);
  return xticks;
}

// sys_shutdown 系统调用：关闭 QEMU。
// 这是一个 xv6 特有的、非标准的系统调用，用于方便地关闭模拟器。
uint64
sys_shutdown(void)
{
  // 调用在 power.c 中定义的 shutdown 函数。
  shutdown();
  return 0; // shutdown 不会返回。
}
