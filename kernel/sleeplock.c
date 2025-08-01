// 睡眠锁（Sleeping locks）
//
// 睡眠锁是一种同步原语，用于保护可能需要长时间持有的资源。
// 与自旋锁（spinlock）不同，当一个进程尝试获取一个已经被占用的睡眠锁时，
// 它不会在CPU上空转等待（spinning），而是会进入休眠状态（SLEEPING），
// 将CPU让给其他可运行的进程。当锁被释放时，等待的进程之一会被唤醒。
//
// 这种机制适用于锁的持有时间可能较长的情况，例如在文件系统操作或I/O期间。
// 在这些场景下，让等待的进程休眠比空转更有效率，因为它避免了浪费CPU周期。
//
// 每个睡眠锁内部包含一个自旋锁，用于保护睡眠锁自身的状态（如`locked`字段）。
// 获取和释放睡眠锁的操作必须是原子的，这个内部自旋锁确保了这一点。

#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "proc.h"
#include "sleeplock.h"

// 初始化一个睡眠锁。
// @param lk: 指向要初始化的睡眠锁的指针。
// @param name: 锁的名称，主要用于调试。
void
initsleeplock(struct sleeplock *lk, char *name)
{
  // 初始化内部的自旋锁，该自旋锁用于保护睡眠锁自身的状态字段。
  initlock(&lk->lk, "sleep lock");
  lk->name = name;
  lk->locked = 0; // 初始状态为未锁定。
  lk->pid = 0;    // 初始没有进程持有该锁。
}

// 获取（或“加锁”）指定的睡眠锁。
// 如果锁已经被其他进程持有，当前进程将会休眠，直到锁被释放。
// @param lk: 指向要获取的睡眠锁的指针。
void
acquiresleep(struct sleeplock *lk)
{
  // 首先获取内部的自旋锁，以确保对睡眠锁状态（lk->locked）的检查和修改是原子操作，
  // 防止多个进程同时尝试获取该睡眠锁时产生竞态条件。
  acquire(&lk->lk);
  // 循环检查锁是否被占用。
  while (lk->locked) {
    // 如果锁已被占用（lk->locked为1），则调用sleep()函数让当前进程休眠。
    // sleep()函数会原子地执行两个操作：
    // 1. 释放lk->lk这个自旋锁。
    // 2. 将当前进程的状态设置为SLEEPING，并将其放入等待队列（等待在`lk`这个通道上）。
    // 当有其他进程调用wakeup(lk)时，此进程会被唤醒，并从sleep()函数返回。
    // 在返回之前，sleep()会重新获取lk->lk这个自旋锁。
    // 这种原子操作避免了“丢失的唤醒”（lost wakeup）问题。
    sleep(lk, &lk->lk);
  }
  // 当循环结束，说明锁当前未被占用，可以获取该锁。
  lk->locked = 1; // 标记锁为已占用。
  lk->pid = myproc()->pid; // 记录持有该锁的进程ID。
  // 释放内部自旋锁，因为对睡眠锁状态的修改已经完成。
  release(&lk->lk);
}

// 释放指定的睡眠锁。
// @param lk: 指向要释放的睡眠锁的指针。
void
releasesleep(struct sleeplock *lk)
{
  // 获取内部自旋锁，以保证对睡眠锁状态的修改是原子的。
  acquire(&lk->lk);
  // 修改锁的状态。
  lk->locked = 0; // 标记锁为未占用。
  lk->pid = 0;    // 清除持有者进程ID。
  // 唤醒一个或多个正在该锁的通道（lk）上休眠的进程。
  // wakeup()会遍历进程表，将所有等待在lk通道上的进程状态从SLEEPING改回RUNNABLE。
  // 虽然可能有多个进程被唤醒，但只有一个能首先通过acquiresleep中的循环并获得锁。
  // 其他被唤醒的进程会在acquiresleep中重新检查lk->locked，发现锁又被占用了，于是会再次休眠。
  wakeup(lk);
  // 释放内部自旋锁。
  release(&lk->lk);
}

// 检查当前进程是否持有指定的睡眠锁。
// @param lk: 指向要检查的睡眠锁的指针。
// @return: 如果当前进程持有该锁，返回1；否则返回0。
int
holdingsleep(struct sleeplock *lk)
{
  int r;
  
  // 获取内部自旋锁，以安全地读取睡眠锁的状态。
  acquire(&lk->lk);
  // 检查锁是否被标记为“已锁定”，并且持有锁的进程ID是否为当前进程的ID。
  r = lk->locked && (lk->pid == myproc()->pid);
  // 释放内部自旋锁。
  release(&lk->lk);
  return r;
}
