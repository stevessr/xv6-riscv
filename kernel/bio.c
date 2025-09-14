// Buffer cache.
// 缓冲区缓存。
//
// The buffer cache is a linked list of buf structures holding
// cached copies of disk block contents.  Caching disk blocks
// in memory reduces the number of disk reads and also provides
// a synchronization point for disk blocks used by multiple processes.
// 缓冲区缓存是一个buf结构的链表，持有磁盘块内容的缓存副本。
// 在内存中缓存磁盘块可以减少磁盘读取次数，也为多个进程使用的磁盘块提供了一个同步点。
//
// Interface:
// 接口：
// * To get a buffer for a particular disk block, call bread.
//   要获取特定磁盘块的缓冲区，请调用bread。
// * After changing buffer data, call bwrite to write it to disk.
//   更改缓冲区数据后，调用bwrite将其写入磁盘。
// * When done with the buffer, call brelse.
//   使用完缓冲区后，调用brelse。
// * Do not use the buffer after calling brelse.
//   调用brelse后不要使用该缓冲区。
// * Only one process at a time can use a buffer,
//   一次只有一个进程可以使用一个缓冲区，
//     so do not keep them longer than necessary.
//   所以不要不必要地长时间持有它们。

#include "types.h"
#include "param.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "riscv.h"
#include "defs.h"
#include "fs.h"
#include "buf.h"

struct
{
  struct spinlock lock; // 保护bcache的自旋锁
  struct buf buf[NBUF]; // NBUF个缓冲区

  // Linked list of all buffers, through prev/next.
  // 所有缓冲区的链表，通过prev/next连接。
  // Sorted by how recently the buffer was used.
  // 按缓冲区最近使用情况排序。
  // head.next is most recent, head.prev is least.
  // head.next是最近使用的，head.prev是最不最近使用的。
  struct buf head;
} bcache;

void binit(void)
{
  struct buf *b;

  initlock(&bcache.lock, "bcache"); // 初始化bcache的锁

  // Create linked list of buffers
  // 创建缓冲区链表
  bcache.head.prev = &bcache.head;
  bcache.head.next = &bcache.head;
  for (b = bcache.buf; b < bcache.buf + NBUF; b++)
  {
    b->next = bcache.head.next;
    b->prev = &bcache.head;
    initsleeplock(&b->lock, "buffer"); // 初始化每个缓冲区的睡眠锁
    bcache.head.next->prev = b;
    bcache.head.next = b;
  }
}

// Look through buffer cache for block on device dev.
// 在缓冲区缓存中查找设备dev上的块。
// If not found, allocate a buffer.
// 如果未找到，则分配一个缓冲区。
// In either case, return locked buffer.
// 无论哪种情况，都返回锁定的缓冲区。
static struct buf *
bget(uint dev, uint blockno)
{
  struct buf *b;

  acquire(&bcache.lock); // 获取bcache的锁

  // Is the block already cached?
  // 块是否已缓存？
  for (b = bcache.head.next; b != &bcache.head; b = b->next)
  {
    if (b->dev == dev && b->blockno == blockno)
    {
      b->refcnt++;            // 增加引用计数
      release(&bcache.lock);  // 释放bcache的锁
      acquiresleep(&b->lock); // 获取缓冲区的睡眠锁
      return b;
    }
  }

  // Not cached.
  // 未缓存。
  // Recycle the least recently used (LRU) unused buffer.
  // 回收最近最少使用（LRU）的未使用缓冲区。
  for (b = bcache.head.prev; b != &bcache.head; b = b->prev)
  {
    if (b->refcnt == 0)
    { // 如果引用计数为0
      b->dev = dev;
      b->blockno = blockno;
      b->valid = 0; // 标记为无效，因为我们将从磁盘读取
      b->refcnt = 1;
      release(&bcache.lock);  // 释放bcache的锁
      acquiresleep(&b->lock); // 获取缓冲区的睡眠锁
      return b;
    }
  }
  panic("bget: no buffers"); // 恐慌：没有可用的缓冲区
}

// Return a locked buf with the contents of the indicated block.
// 返回一个锁定的buf，其中包含所指示块的内容。
struct buf *
bread(uint dev, uint blockno)
{
  struct buf *b;

  b = bget(dev, blockno); // 获取缓冲区
  if (!b->valid)
  {                       // 如果缓冲区内容无效
    virtio_disk_rw(b, 0); // 从磁盘读取数据到缓冲区（0表示读）
    b->valid = 1;         // 标记为有效
  }
  return b;
}

// Write b's contents to disk.  Must be locked.
// 将b的内容写入磁盘。必须被锁定。
void bwrite(struct buf *b)
{
  if (!holdingsleep(&b->lock))
    panic("bwrite");    // 恐慌：bwrite时未持有锁
  virtio_disk_rw(b, 1); // 将缓冲区数据写入磁盘（1表示写）
}

// Release a locked buffer.
// 释放一个锁定的缓冲区。
// Move to the head of the most-recently-used list.
// 移动到最近使用列表的头部。
void brelse(struct buf *b)
{
  if (!holdingsleep(&b->lock))
    panic("brelse"); // 恐慌：brelse时未持有锁

  releasesleep(&b->lock); // 释放缓冲区的睡眠锁

  acquire(&bcache.lock); // 获取bcache的锁
  b->refcnt--;           // 减少引用计数
  if (b->refcnt == 0)
  {
    // no one is waiting for it.
    // 没有进程在等待它。
    b->next->prev = b->prev;
    b->prev->next = b->next;
    b->next = bcache.head.next;
    b->prev = &bcache.head;
    bcache.head.next->prev = b;
    bcache.head.next = b;
  }

  release(&bcache.lock); // 释放bcache的锁
}

void bpin(struct buf *b)
{
  acquire(&bcache.lock); // 获取bcache的锁
  b->refcnt++;           // 增加引用计数
  release(&bcache.lock); // 释放bcache的lock
}

void bunpin(struct buf *b)
{
  acquire(&bcache.lock); // 获取bcache的锁
  b->refcnt--;           // 减少引用计数
  release(&bcache.lock); // 释放bcache的锁
}