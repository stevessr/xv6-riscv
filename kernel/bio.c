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

// bcache 结构体定义了缓冲区缓存的全局状态。
struct {
  struct spinlock lock;       // 用于保护 bcache 内部数据（如链表）的自旋锁。
  struct buf buf[NBUF];       // 包含 NBUF 个缓冲区的数组，NBUF 在 param.h 中定义。

  // 所有缓冲区的链表，通过 prev/next 指针连接。
  // 这个链表用于实现 LRU (Least Recently Used) 策略。
  // head.next 指向最近使用的缓冲区。
  // head.prev 指向最久未使用的缓冲区。
  struct buf head;
} bcache;

// binit 初始化缓冲区缓存。
void
binit(void)
{
  struct buf *b;

  // 初始化 bcache 的自旋锁，名称为 "bcache"。
  initlock(&bcache.lock, "bcache");

  // 创建缓冲区的双向循环链表。
  // 初始时，head 的 prev 和 next 都指向自己，形成一个空链表。
  bcache.head.prev = &bcache.head;
  bcache.head.next = &bcache.head;
  // 遍历 buf 数组，将每个缓冲区添加到链表的头部。
  for(b = bcache.buf; b < bcache.buf+NBUF; b++){
    b->next = bcache.head.next; // 新缓冲区 b 的 next 指向原来的第一个缓冲区。
    b->prev = &bcache.head;     // 新缓冲区 b 的 prev 指向 head。
    initsleeplock(&b->lock, "buffer"); // 为每个缓冲区初始化一个休眠锁。
    bcache.head.next->prev = b; // 原来的第一个缓冲区的 prev 指向新的缓冲区 b。
    bcache.head.next = b;       // head 的 next 指向新的缓冲区 b，使其成为新的第一个元素。
  }
}

// bget 在缓冲区缓存中查找一个用于设备 dev 和块号 blockno 的缓冲区。
// 如果找到了一个匹配的缓冲区，就返回它。
// 如果没有找到，就从 LRU 链表中找一个未被使用的缓冲区，并将其分配给指定的 dev 和 blockno。
// 无论哪种情况，返回的缓冲区都是被锁定的。
static struct buf*
bget(uint dev, uint blockno)
{
  struct buf *b;

  // 获取 bcache 的自旋锁，以保护对缓冲区链表的并发访问。
  acquire(&bcache.lock);

  // 1. 检查块是否已经被缓存。
  // 从链表头（最近使用的）开始遍历。
  for(b = bcache.head.next; b != &bcache.head; b = b->next){
    if(b->dev == dev && b->blockno == blockno){
      // 如果找到了匹配的缓冲区，增加其引用计数。
      b->refcnt++;
      // 释放 bcache 锁，因为已经找到了缓冲区，不再需要操作链表。
      release(&bcache.lock);
      // 获取该缓冲区的休眠锁。这会使进程休眠，直到该缓冲区可用。
      acquiresleep(&b->lock);
      // 返回锁定的缓冲区。
      return b;
    }
  }

  // 2. 如果块未被缓存。
  // 从链表尾（最久未使用的）开始遍历，寻找一个可以回收的缓冲区。
  for(b = bcache.head.prev; b != &bcache.head; b = b->prev){
    if(b->refcnt == 0) { // 找到一个未被任何进程引用的缓冲区。
      // 分配这个缓冲区给新的设备和块号。
      b->dev = dev;
      b->blockno = blockno;
      b->valid = 0; // 标记内容为无效，因为它尚未从磁盘加载。
      b->refcnt = 1; // 设置引用计数为 1。
      // 释放 bcache 锁。
      release(&bcache.lock);
      // 获取该缓冲区的休眠锁。
      acquiresleep(&b->lock);
      // 返回新分配并锁定的缓冲区。
      return b;
    }
  }
  
  // 如果所有缓冲区都已被引用，说明系统资源不足。
  panic("bget: no buffers");
}

// bread 返回一个锁定的缓冲区，其中包含设备 dev 上块 blockno 的内容。
// 如果缓冲区内容无效，它会从磁盘读取数据。
struct buf*
bread(uint dev, uint blockno)
{
  struct buf *b;

  // 获取一个用于指定设备和块号的缓冲区。
  b = bget(dev, blockno);
  // 检查缓冲区的数据是否有效。
  if(!b->valid) {
    // 如果数据无效，调用 virtio_disk_rw 从磁盘读取数据到缓冲区。
    // 第二个参数 0 表示读取操作。
    virtio_disk_rw(b, 0);
    // 将缓冲区标记为有效。
    b->valid = 1;
  }
  // 返回包含有效数据的锁定缓冲区。
  return b;
}

// bwrite 将缓冲区 b 的内容写入磁盘。
// 调用者必须持有该缓冲区的锁。
void
bwrite(struct buf *b)
{
  // 确保调用者持有缓冲区的休眠锁。
  if(!holdingsleep(&b->lock))
    panic("bwrite");
  // 调用 virtio_disk_rw 将缓冲区数据写入磁盘。
  // 第二个参数 1 表示写入操作。
  virtio_disk_rw(b, 1);
}

// brelse 释放一个锁定的缓冲区。
// 它会减少缓冲区的引用计数，并在引用计数为零时将其移动到 LRU 链表的头部。
void
brelse(struct buf *b)
{
  // 确保调用者持有缓冲区的休眠锁。
  if(!holdingsleep(&b->lock))
    panic("brelse");

  // 释放缓冲区的休眠锁，允许其他进程使用它。
  releasesleep(&b->lock);

  // 获取 bcache 的自旋锁，以安全地修改引用计数和链表。
  acquire(&bcache.lock);
  // 减少引用计数。
  b->refcnt--;
  if (b->refcnt == 0) {
    // 如果没有其他进程引用此缓冲区，
    // 将其移动到 LRU 链表的头部（最近使用的位置）。
    b->next->prev = b->prev;
    b->prev->next = b->next;
    b->next = bcache.head.next;
    b->prev = &bcache.head;
    bcache.head.next->prev = b;
    bcache.head.next = b;
  }
  
  // 释放 bcache 锁。
  release(&bcache.lock);
}

// bpin 增加缓冲区的引用计数。
// 这可以防止缓冲区被 bget 回收。
void
bpin(struct buf *b) {
  acquire(&bcache.lock);
  b->refcnt++;
  release(&bcache.lock);
}

// bunpin 减少缓冲区的引用计数。
void
bunpin(struct buf *b) {
  acquire(&bcache.lock);
  b->refcnt--;
  release(&bcache.lock);
}
