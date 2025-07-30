struct file {
  enum { FD_NONE, FD_PIPE, FD_INODE, FD_DEVICE } type; // 文件类型
  int ref;           // 引用计数
  char readable;     // 是否可读
  char writable;     // 是否可写
  struct pipe *pipe; // FD_PIPE
  struct inode *ip;  // FD_INODE 和 FD_DEVICE
  uint off;          // FD_INODE
  short major;       // FD_DEVICE
};

// 从设备号中提取主设备号
#define major(dev)  ((dev) >> 16 & 0xFFFF)
// 从设备号中提取次设备号
#define minor(dev)  ((dev) & 0xFFFF)
// 根据主次设备号创建设备号
#define	mkdev(m,n)  ((uint)((m)<<16| (n)))

// inode 的内存副本
struct inode {
  uint dev;           // 设备号
  uint inum;          // Inode 号
  int ref;            // 引用计数
  struct sleeplock lock; // 保护以下所有字段
  int valid;          // inode 是否已从磁盘读取?

  short type;         // 磁盘 inode 的副本
  short major;
  short minor;
  short nlink;
  uint size;
  uint addrs[NDIRECT+1];
};

// 将主设备号映射到设备功能。
struct devsw {
  int (*read)(int, uint64, int);
  int (*write)(int, uint64, int);
};

extern struct devsw devsw[];

#define CONSOLE 1
