// 睡眠锁（Sleeping locks）

#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "proc.h"
#include "sleeplock.h"

// 初始化一个睡眠锁
void
initsleeplock(struct sleeplock *lk, char *name)
{
  initlock(&lk->lk, "sleep lock"); // 初始化内部的自旋锁
  lk->name = name;
  lk->locked = 0;
  lk->pid = 0;
}

// 获取睡眠锁
// 如果锁已被占用，则当前进程会休眠
void
acquiresleep(struct sleeplock *lk)
{
  acquire(&lk->lk); // 获取内部自旋锁以保证原子性操作
  while (lk->locked) {
    // 当锁被其他进程持有时，调用 sleep 函数进行休眠。
    // sleep 会原子地释放 lk->lk 并让当前进程休眠在 lk 这个通道上。
    // 当被唤醒时，sleep 会重新获取 lk->lk。
    sleep(lk, &lk->lk);
  }
  // 获取到锁
  lk->locked = 1;
  lk->pid = myproc()->pid;
  release(&lk->lk); // 释放内部自旋锁
}

// 释放睡眠锁
void
releasesleep(struct sleeplock *lk)
{
  acquire(&lk->lk); // 获取内部自旋锁以保证原子性操作
  lk->locked = 0;
  lk->pid = 0;
  // 唤醒所有可能在此锁上休眠的进程。
  // 只有一个进程能真正获取到锁，其他被唤醒的进程会发现锁仍然被占用，然后继续休眠。
  wakeup(lk);
  release(&lk->lk); // 释放内部自旋锁
}

// 检查当前进程是否持有该睡眠锁
int
holdingsleep(struct sleeplock *lk)
{
  int r;
  
  acquire(&lk->lk);
  r = lk->locked && (lk->pid == myproc()->pid);
  release(&lk->lk);
  return r;
}
