// 缓冲区缓存 (Buffer cache)
//
// 缓冲区缓存是一个 `buf` 结构的链表，持有磁盘块内容的缓存副本。
// 在内存中缓存磁盘块可以减少磁盘读取次数，
// 并为多个进程使用的磁盘块提供同步点。
//
// 接口:
// * 要获取特定磁盘块的缓冲区，请调用 bread。
// * 更改缓冲区数据后，调用 bwrite 将其写入磁盘。
// * 使用完缓冲区后，调用 brelse。
// * 调用 brelse 后不要再使用该缓冲区。
// * 一次只有一个进程可以使用一个缓冲区，
//   因此不要持有它们超过必要的时间。


#include "types.h"
#include "param.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "riscv.h"
#include "defs.h"
#include "fs.h"
#include "buf.h"

// 缓冲区缓存的全局数据结构
struct {
  struct spinlock lock;       // 保护 bcache 的锁
  struct buf buf[NBUF];       // 缓冲区数组

  // 所有缓冲区的链表，通过 prev/next 连接。
  // 按缓冲区最近使用情况排序。
  // head.next 是最近使用的，head.prev 是最久未使用的。
  struct buf head;
} bcache;

void
binit(void)
{
  struct buf *b;

  initlock(&bcache.lock, "bcache"); // 初始化 bcache 锁

  // 创建缓冲区链表
  bcache.head.prev = &bcache.head;
  bcache.head.next = &bcache.head;
  for(b = bcache.buf; b < bcache.buf+NBUF; b++){
    b->next = bcache.head.next;
    b->prev = &bcache.head;
    initsleeplock(&b->lock, "buffer"); // 初始化每个缓冲区的休眠锁
    bcache.head.next->prev = b;
    bcache.head.next = b;
  }
}

// 在缓冲区缓存中查找设备 dev 上的块 block
// 如果未找到，则分配一个缓冲区。
// 无论哪种情况，都返回一个锁定的缓冲区。
static struct buf*
bget(uint dev, uint blockno)
{
  struct buf *b;

  acquire(&bcache.lock); // 获取 bcache 锁

  // 块是否已经被缓存？
  for(b = bcache.head.next; b != &bcache.head; b = b->next){
    if(b->dev == dev && b->blockno == blockno){
      b->refcnt++; // 增加引用计数
      release(&bcache.lock); // 释放 bcache 锁
      acquiresleep(&b->lock); // 获取该缓冲区的休眠锁
      return b;
    }
  }

  // 未缓存。
  // 回收最近最少使用（LRU）的未使用缓冲区。
  for(b = bcache.head.prev; b != &bcache.head; b = b->prev){
    if(b->refcnt == 0) { // 找到一个未被引用的缓冲区
      b->dev = dev;
      b->blockno = blockno;
      b->valid = 0; // 标记为无效，因为内容还未从磁盘读取
      b->refcnt = 1;
      release(&bcache.lock); // 释放 bcache 锁
      acquiresleep(&b->lock); // 获取该缓冲区的休眠锁
      return b;
    }
  }
  panic("bget: no buffers"); // 恐慌：没有可用的缓冲区
}

// 返回一个锁定的 buf，其中包含指定块的内容。
struct buf*
bread(uint dev, uint blockno)
{
  struct buf *b;

  b = bget(dev, blockno); // 获取一个缓冲区
  if(!b->valid) { // 如果缓冲区内容无效
    virtio_disk_rw(b, 0); // 从磁盘读取 (0 表示读)
    b->valid = 1; // 标记为有效
  }
  return b;
}

// 将 b 的内容写入磁盘。必须持有锁。
void
bwrite(struct buf *b)
{
  if(!holdingsleep(&b->lock)) // 检查是否持有锁
    panic("bwrite");
  virtio_disk_rw(b, 1); // 写入磁盘 (1 表示写)
}

// 释放一个锁定的缓冲区。
// 移动到最近使用列表的头部。
void
brelse(struct buf *b)
{
  if(!holdingsleep(&b->lock)) // 检查是否持有锁
    panic("brelse");

  releasesleep(&b->lock); // 释放休眠锁

  acquire(&bcache.lock); // 获取 bcache 锁
  b->refcnt--; // 减少引用计数
  if (b->refcnt == 0) {
    // 没有进程在等待它。
    // 将此缓冲区移动到 LRU 列表的前面（最近使用的位置）。
    b->next->prev = b->prev;
    b->prev->next = b->next;
    b->next = bcache.head.next;
    b->prev = &bcache.head;
    bcache.head.next->prev = b;
    bcache.head.next = b;
  }
  
  release(&bcache.lock); // 释放 bcache 锁
}

// 增加缓冲区的引用计数，使其不会被回收
void
bpin(struct buf *b) {
  acquire(&bcache.lock);
  b->refcnt++;
  release(&bcache.lock);
}

// 减少缓冲区的引用计数
void
bunpin(struct buf *b) {
  acquire(&bcache.lock);
  b->refcnt--;
  release(&bcache.lock);
}
