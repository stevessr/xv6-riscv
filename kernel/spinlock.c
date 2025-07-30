// 互斥自旋锁。

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "riscv.h"
#include "proc.h"
#include "defs.h"

// 初始化一个自旋锁
void
initlock(struct spinlock *lk, char *name)
{
  lk->name = name;
  lk->locked = 0;
  lk->cpu = 0;
}

// 获取锁。
// 循环（自旋）直到锁被获取。
void
acquire(struct spinlock *lk)
{
  push_off(); // 禁用中断以避免死锁。
  if(holding(lk))
    panic("acquire");

  // 在 RISC-V 上, __sync_lock_test_and_set 会转换成一个原子交换指令:
  //   a5 = 1
  //   s1 = &lk->locked
  //   amoswap.w.aq a5, a5, (s1)
  // 这条指令会原子地将 1 写入 lk->locked，并返回 lk->locked 的旧值。
  // 循环直到旧值为 0，表示成功获取了锁。
  while(__sync_lock_test_and_set(&lk->locked, 1) != 0)
    ;

  // 告诉 C 编译器和处理器不要将加载或存储操作移动到此点之后，
  // 以确保临界区的内存引用严格发生在锁被获取之后。
  // 在 RISC-V 上, 这会生成一个 fence 指令。
  __sync_synchronize();

  // 记录有关锁获取的信息，用于 holding() 和调试。
  lk->cpu = mycpu();
}

// 释放锁。
void
release(struct spinlock *lk)
{
  if(!holding(lk))
    panic("release");

  lk->cpu = 0;

  // 告诉 C 编译器和 CPU 不要将加载或存储操作移动到此点之前，
  // 以确保临界区中的所有存储对其他 CPU 可见，
  // 并且临界区中的加载严格发生在锁被释放之前。
  // 在 RISC-V 上, 这会生成一个 fence 指令。
  __sync_synchronize();

  // 释放锁, 相当于 lk->locked = 0。
  // 这段代码不使用 C 赋值语句，因为 C 标准
  // 暗示赋值可能由多个存储指令实现。
  // 在 RISC-V 上, sync_lock_release 会转换成一个原子交换:
  //   s1 = &lk->locked
  //   amoswap.w zero, zero, (s1)
  // 这会原子地将 0 写入 lk->locked。
  __sync_lock_release(&lk->locked);

  pop_off();
}

// 检查当前 CPU 是否持有该锁。
// 调用时中断必须是关闭的。
int
holding(struct spinlock *lk)
{
  int r;
  r = (lk->locked && lk->cpu == mycpu());
  return r;
}

// push_off/pop_off 类似于 intr_off()/intr_on()，但它们是配对的：
// 需要两个 pop_off() 来撤销两个 push_off()。此外，如果中断
// 最初是关闭的，那么 push_off, pop_off 会保持中断关闭状态。
// 这用于实现可重入的临界区。

void
push_off(void)
{
  int old = intr_get(); // 保存当前的中断状态

  intr_off(); // 禁用中断
  if(mycpu()->noff == 0)
    mycpu()->intena = old; // 如果是第一次进入临界区，记录之前的中断状态
  mycpu()->noff += 1; // 增加嵌套深度
}

void
pop_off(void)
{
  struct cpu *c = mycpu();
  if(intr_get())
    panic("pop_off - interruptible"); // pop_off 时中断应为禁用状态
  if(c->noff < 1)
    panic("pop_off"); // 嵌套深度不能小于1
  c->noff -= 1; // 减少嵌套深度
  // 如果这是最外层的 pop_off，并且中断最初是开启的，则恢复中断
  if(c->noff == 0 && c->intena)
    intr_on();
}
