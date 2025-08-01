//
// 物理内存页分配器 (Physical Memory Allocator)
//
// xv6 内核使用此分配器来动态申请和释放物理内存。
// 它的工作单位是“页”(Page)，每页大小固定为 4096 字节。
// 这部分内存主要用于：
// 1. 为用户进程分配内存空间。
// 2. 为每个进程创建其内核栈。
// 3. 存储页表 (Page Tables)。
// 4. 作为管道 (Pipes) 的内核缓冲区。
//
// 分配器管理着从内核代码和数据段结束位置(`end`)到物理内存顶端(`PHYSTOP`)之间的所有内存。
// 实现原理是维护一个空闲物理页的单向链表（通常称为 freelist）。

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "riscv.h"
#include "defs.h"

void freerange(void *pa_start, void *pa_end);

extern char end[]; // `end` 是一个由链接器脚本 kernel.ld 定义的符号。
                   // 它并不指向任何实际的数据，而是作为一个地址标记，
                   // 标识了内核代码和静态数据段结束之后第一个字节的位置。
                   // 物理内存分配器将管理从 `end` 到 PHYSTOP 地址之间的所有内存。

// `struct run` 代表一个空闲的物理页。
// 因为页是空闲的，我们可以安全地利用其 4096 字节的空间来存储数据。
// 在这里，我们用它来存储指向下一个空闲页的指针，
// 从而将所有空闲页链接成一个单向链表。
struct run {
  struct run *next;
};

// 内核内存分配器的核心数据结构。
struct {
  struct spinlock lock;   // 自旋锁，用于在多核环境下保护 freelist 的并发访问。
                           // 如果没有锁，多个 CPU 同时调用 kalloc() 或 kfree() 
                           // 可能会导致链表损坏或数据不一致。
  struct run *freelist;    // 指向空闲页链表的头部。
} kmem;

// 初始化物理内存分配器。
void
kinit()
{
  initlock(&kmem.lock, "kmem"); // 初始化分配器的自旋锁。
  // 调用 freerange 函数，将从内核末尾(`end`)到物理内存顶端(`PHYSTOP`)
  // 之间的所有物理内存，逐页释放并添加到空闲链表中。
  freerange(end, (void*)PHYSTOP);
}

// 将一段给定的物理地址范围 [pa_start, pa_end) 逐页添加到空闲链表中。
void
freerange(void *pa_start, void *pa_end)
{
  char *p;
  // 首先，将起始地址向上取整到下一个页边界(PGSIZE的整数倍)。
  // 这是为了确保我们操作的单位都是完整的页。
  p = (char*)PGROUNDUP((uint64)pa_start);
  // 遍历这个地址范围内的每一页。
  for(; p + PGSIZE <= (char*)pa_end; p += PGSIZE)
    kfree(p); // 调用 kfree 将每一页加入到空闲链表的头部。
}

// 释放一个物理页。
// `pa` 是要释放的页的起始物理地址，它通常是之前调用 `kalloc()` 的返回值。
void
kfree(void *pa)
{
  struct run *r;

  // 对传入的地址 `pa` 进行一系列健全性检查：
  // 1. 地址必须是页对齐的 (地址是 PGSIZE 的整数倍)。
  // 2. 地址不能位于内核代码和数据区域（即地址值必须大于 `end`）。
  // 3. 地址必须在物理内存的合法范围内（即小于 PHYSTOP）。
  if(((uint64)pa % PGSIZE) != 0 || (char*)pa < end || (uint64)pa >= PHYSTOP)
    panic("kfree");

  // 将被释放的内存块用固定的值(1)填充。这是一种调试技巧，被称为“内存投毒”(Memory Poisoning)。
  // 如果有代码在释放后仍然非法地使用这块内存（即“悬挂指针”问题），
  // 它会读到无意义的数据(1)而不是旧的有效数据，这使得这类错误更容易被发现。
  memset(pa, 1, PGSIZE);

  // 将物理地址 `pa` 强制转换为 `struct run*` 类型，以便将其作为链表节点处理。
  r = (struct run*)pa;

  acquire(&kmem.lock);      // 获取自旋锁，准备修改 freelist。
  r->next = kmem.freelist;  // 将新释放的页的 next 指针指向当前的 freelist 头部。
  kmem.freelist = r;        // 更新 freelist 的头，使其指向这个新释放的页。
  release(&kmem.lock);      // 释放锁。
}

// 分配一页（4096字节）的物理内存。
// 成功时，返回一个指向该页起始地址的指针，该指针可被内核直接使用。
// 如果系统中所有内存页都已被分配，则返回 0 (空指针)。
void *
kalloc(void)
{
  struct run *r;

  acquire(&kmem.lock);        // 获取锁，以安全地访问 freelist。
  r = kmem.freelist;          // 从 freelist 头部取出一个空闲页。
  if(r)                       // 如果 freelist 不为空 (即 r != null)...
    kmem.freelist = r->next;  // ...则将 freelist 的头更新为下一个节点。
  release(&kmem.lock);        // 释放锁。

  if(r) {
    // 如果成功分配到了页 (r 不为 null)，也对其进行“内存投毒”。
    // 将其内容填充为 5。这有助于捕获那些错误地假设新分配内存被自动清零的代码。
    // 正确的实践是，内核代码在使用新分配的内存前，应该根据需要进行显式地初始化。
    memset((char*)r, 5, PGSIZE); 
  }
  
  return (void*)r; // 返回分配到的页的地址，或者在失败时返回空指针。
}
