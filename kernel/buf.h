struct buf {
  int valid;   // 数据是否已从磁盘读取？
  int disk;    // 磁盘是否“拥有”该缓冲区？
  uint dev;
  uint blockno;
  struct sleeplock lock;
  uint refcnt;
  struct buf *prev; // LRU 缓存列表
  struct buf *next;
  uchar data[BSIZE];
};

