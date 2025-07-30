// 物理内存分配器，用于用户进程、
// 内核栈、页表页、
// 和管道缓冲区。分配完整的 4096 字节页。

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "riscv.h"
#include "defs.h"

void freerange(void *pa_start, void *pa_end);

extern char end[]; // 内核结束后的第一个地址。
                   // 由 kernel.ld 定义。

// 空闲物理页的链表节点
struct run {
  struct run *next;
};

// 物理内存分配器的数据结构
struct {
  struct spinlock lock;
  struct run *freelist; // 空闲页链表的头指针
} kmem;

void
kinit()
{
  initlock(&kmem.lock, "kmem"); // 初始化锁
  // 将从内核结束到物理内存顶端的内存全部释放
  freerange(end, (void*)PHYSTOP);
}

// 释放一段范围的物理内存
void
freerange(void *pa_start, void *pa_end)
{
  char *p;
  // 将起始地址向上对齐到页边界
  p = (char*)PGROUNDUP((uint64)pa_start);
  for(; p + PGSIZE <= (char*)pa_end; p += PGSIZE)
    kfree(p);
}

// 释放 pa 指向的物理内存页，
// 这个 pa 通常应该是 kalloc() 调用返回的。
// (例外情况是初始化分配器时；见上面的 kinit)
void
kfree(void *pa)
{
  struct run *r;

  // 检查地址的合法性
  if(((uint64)pa % PGSIZE) != 0 || (char*)pa < end || (uint64)pa >= PHYSTOP)
    panic("kfree");

  // 用垃圾数据填充以捕获悬空引用。
  memset(pa, 1, PGSIZE);

  r = (struct run*)pa;

  acquire(&kmem.lock);
  // 将释放的页添加到空闲链表的头部
  r->next = kmem.freelist;
  kmem.freelist = r;
  release(&kmem.lock);
}

// 分配一个 4096 字节的物理内存页。
// 返回一个内核可以使用的指针。
// 如果无法分配内存，则返回 0。
void *
kalloc(void)
{
  struct run *r;

  acquire(&kmem.lock);
  r = kmem.freelist;
  if(r)
    // 从空闲链表中取下一个节点
    kmem.freelist = r->next;
  release(&kmem.lock);

  if(r)
    // 用垃圾数据填充以帮助发现bug
    memset((char*)r, 5, PGSIZE); 
  return (void*)r;
}
