// 互斥自旋锁 (Mutual exclusion spin locks)
//
// xv6 使用自旋锁来保护多个 CPU 同时访问的关键数据结构。
// 自旋锁是一种简单的锁，它通过在一个循环中持续检查锁的状态（“自旋”）直到锁可用为止。
// 在持有自旋锁时，代码不能放弃 CPU，因此自旋锁只应在持有时间非常短的情况下使用。
// 此外，在持有锁时必须禁用中断，以防止中断处理程序尝试获取同一个锁而导致死锁。

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "riscv.h"
#include "proc.h"
#include "defs.h"

// 初始化一个自旋锁。
// @param lk: 指向 spinlock 结构的指针。
// @param name: 锁的名称，用于调试。
void
initlock(struct spinlock *lk, char *name)
{
  lk->name = name;
  lk->locked = 0;  // 初始状态为未锁定
  lk->cpu = 0;     // 初始没有被任何 CPU 持有
}

// 获取锁。
// 在一个循环中“自旋”，直到成功获取锁。
// 这个函数在返回前必须确保锁已经被当前 CPU 持有。
// @param lk: 指向 spinlock 结构的指针。
void
acquire(struct spinlock *lk)
{
  // 在尝试获取锁之前禁用中断。
  // 这是为了防止当前线程被中断，而中断处理程序又尝试获取同一个锁，从而导致死锁。
  // 例如：CPU1 持有锁 L，然后发生中断。中断处理程序也尝试获取锁 L，
  // 但由于 L 已经被持有，处理程序会一直自旋等待，永远无法返回，
  // 导致 CPU1 也无法释放锁。
  push_off();
  // 检查当前 CPU 是否已经持有该锁，防止重入（一个线程重复获取同一个锁）。
  if(holding(lk))
    panic("acquire");

  // 使用 GCC 内置的原子操作 `__sync_lock_test_and_set` 来尝试获取锁。
  // 这个函数会原子地将 `lk->locked` 设置为 1，并返回 `lk->locked` 的旧值。
  // 在 RISC-V 上，它通常被编译为 `amoswap.w.aq` 指令。
  // `amoswap` 指令原子地将一个新值写入内存地址，并加载该地址的旧值。
  // `.aq` (acquire) 后缀是一个内存屏障，确保后续的内存操作不会被重排序到此指令之前。
  //
  // 循环会一直执行，直到 `__sync_lock_test_and_set` 返回 0，
  // 这意味着在原子操作之前 `lk->locked` 的值是 0 (未锁定)，现在我们成功地将其设置为 1 (锁定)。
  while(__sync_lock_test_and_set(&lk->locked, 1) != 0)
    ;

  // 使用一个完全的内存屏障 `__sync_synchronize`。
  // 这个屏障确保在它之前的内存写操作（即锁的获取）对所有 CPU 可见，
  // 并且在它之后的内存读/写操作不会被编译器或 CPU 重排序到它之前。
  // 这对于保证临界区内的代码严格在锁被获取之后执行至关重要。
  // 在 RISC-V 上，这会生成一个 `fence` 指令。
  __sync_synchronize();

  // 记录是哪个 CPU 获取了锁。
  // 这对于调试和 `holding()` 函数的实现非常重要。
  lk->cpu = mycpu();
}

// 释放锁。
// @param lk: 指向 spinlock 结构的指针。
void
release(struct spinlock *lk)
{
  // 检查当前 CPU 是否真的持有该锁。如果一个 CPU 尝试释放一个它不持有的锁，
  // 这通常是一个严重的 bug，所以直接 panic。
  if(!holding(lk))
    panic("release");

  // 清除持有锁的 CPU 信息。
  lk->cpu = 0;

  // 再次使用一个完全的内存屏障 `__sync_synchronize`。
  // 这确保了在临界区内所有的内存写操作都已完成，并对其他 CPU 可见，
  // 然后才真正释放锁。同样，这也防止了锁的释放操作被重排序到临界区代码之前。
  __sync_synchronize();

  // 使用 GCC 内置的原子操作 `__sync_lock_release` 来释放锁。
  // 这个函数原子地将 `lk->locked` 设置为 0。
  // 它之所以被使用，是因为一个简单的 C 语言赋值 `lk->locked = 0;` 不保证是原子的。
  // 在 RISC-V 上，它被编译为 `amoswap.w.rl` 指令。
  // `.rl` (release) 后缀是一个内存屏障，确保在此指令之前的内存操作不会被重排序到它之后。
  __sync_lock_release(&lk->locked);

  // 恢复之前的中断状态。
  pop_off();
}

// 检查当前 CPU 是否持有该锁。
// 调用此函数时中断必须是禁用的，以确保检查的原子性。
// @param lk: 指向 spinlock 结构的指针。
// @return: 如果当前 CPU 持有锁，则返回 1，否则返回 0。
int
holding(struct spinlock *lk)
{
  int r;
  // 只有当锁被标记为 `locked` 并且持有锁的 CPU 是当前 CPU 时，才认为当前 CPU 持有该锁。
  r = (lk->locked && lk->cpu == mycpu());
  return r;
}

// push_off/pop_off: 用于管理中断状态的嵌套调用。
//
// `push_off` 会禁用中断，并将 `noff` 计数器加一。
// 如果这是第一次调用 `push_off`（即 `noff` 从 0 变为 1），它会保存当前的中断状态（开启或关闭）。
//
// `pop_off` 会将 `noff` 计数器减一。
// 只有当 `noff` 变回 0 时，它才会根据之前保存的状态决定是否要重新启用中断。
//
// 这种设计允许 `acquire` 和 `release` 的临界区可以嵌套。例如：
// acquire(&lock1);
// acquire(&lock2);
// ...
// release(&lock2);
// release(&lock1);
// 在这个过程中，中断只会在最外层的 `acquire` 中被禁用，并在最外层的 `release` 中被恢复。

// 压入一层中断禁用。
void
push_off(void)
{
  int old = intr_get(); // 获取当前的中断状态 (sstatus.SIE)

  intr_off(); // 禁用中断
  // 如果这是此 CPU 上的第一次 push_off 调用
  if(mycpu()->noff == 0)
    mycpu()->intena = old; // 保存原始的中断状态
  mycpu()->noff += 1; // 增加嵌套层数
}

// 弹出一层中断禁用。
void
pop_off(void)
{
  struct cpu *c = mycpu();
  if(intr_get())
    panic("pop_off - interruptible"); // 在调用 pop_off 时，中断应该总是禁用的
  if(c->noff < 1)
    panic("pop_off"); // 不应出现 pop_off 调用次数多于 push_off 的情况
  c->noff -= 1; // 减少嵌套层数
  // 如果嵌套层数回到 0，并且之前的中断状态是开启的，那么就恢复中断
  if(c->noff == 0 && c->intena)
    intr_on();
}
