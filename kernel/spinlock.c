//-*- coding: utf-8 -*-
// 互斥自旋锁。

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "riscv.h"
#include "proc.h"
#include "defs.h"

void initlock(struct spinlock *lk, char *name)
{
  lk->name = name;
  lk->locked = 0;
  lk->cpu = 0;
}

// 获取锁。
// 循环（自旋）直到获取锁。
void acquire(struct spinlock *lk)
{
  push_off(); // 禁用中断以避免死锁。
  if (holding(lk))
    panic("acquire");

  // 在RISC-V上，sync_lock_test_and_set会变成一个原子交换：
  //   a5 = 1
  //   s1 = &lk->locked
  //   amoswap.w.aq a5, a5, (s1)
  while (__sync_lock_test_and_set(&lk->locked, 1) != 0)
    ;

  // 告诉C编译器和处理器不要将加载或存储操作移过此点，
  // 以确保临界区的内存引用严格在获取锁之后发生。
  // 在RISC-V上，这会发出一条fence指令。
  __sync_synchronize();

  // 记录有关锁获取的信息，用于holding()和调试。
  lk->cpu = mycpu();
}

// 释放锁。
void release(struct spinlock *lk)
{
  if (!holding(lk))
    panic("release");

  lk->cpu = 0;

  // 告诉C编译器和CPU不要将加载或存储操作移过此点，
  // 以确保临界区中的所有存储在锁被释放之前对其他CPU可见，
  // 并且临界区中的加载严格在锁被释放之前发生。
  // 在RISC-V上，这会发出一条fence指令。
  __sync_synchronize();

  // 释放锁，相当于 lk->locked = 0。
  // 此代码不使用C赋值，因为C标准意味着
  // 赋值可能由多个存储指令实现。
  // 在RISC-V上，sync_lock_release会变成一个原子交换：
  //   s1 = &lk->locked
  //   amoswap.w zero, zero, (s1)
  __sync_lock_release(&lk->locked);

  pop_off();
}

// 检查此cpu是否持有该锁。
// 中断必须关闭。
int holding(struct spinlock *lk)
{
  int r;
  r = (lk->locked && lk->cpu == mycpu());
  return r;
}

// push_off/pop_off 类似于 intr_off()/intr_on()，但它们是配对的：
// 需要两个 pop_off() 才能撤销两个 push_off()。此外，如果中断
// 最初是关闭的，那么 push_off, pop_off 会让它们保持关闭。

void push_off(void)
{
  int old = intr_get();

  // 禁用中断以防止在使用mycpu()时发生非自愿的上下文切换。
  intr_off();

  if (mycpu()->noff == 0)
    mycpu()->intena = old;
  mycpu()->noff += 1;
}

void pop_off(void)
{
  struct cpu *c = mycpu();
  if (intr_get())
    panic("pop_off - interruptible");
  if (c->noff < 1)
    panic("pop_off");
  c->noff -= 1;
  if (c->noff == 0 && c->intena)
    intr_on();
}