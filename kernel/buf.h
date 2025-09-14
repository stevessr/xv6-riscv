struct buf
{
  int valid;             // 数据是否已从磁盘读取？
  int disk;              // 磁盘是否“拥有”buf？
  uint dev;              // 设备号
  uint blockno;          // 块号
  struct sleeplock lock; // 保护缓冲区的睡眠锁
  uint refcnt;           // 引用计数
  struct buf *prev;      // LRU缓存列表中的前一个缓冲区
  struct buf *next;      // LRU缓存列表中的后一个缓冲区
  uchar data[BSIZE];     // 缓存的数据
};