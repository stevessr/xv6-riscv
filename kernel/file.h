struct file
{
  enum
  {
    FD_NONE,
    FD_PIPE,
    FD_INODE,
    FD_DEVICE
  } type;            // 文件类型
  int ref;           // 引用计数
  char readable;     // 是否可读
  char writable;     // 是否可写
  struct pipe *pipe; // 如果是管道文件，指向管道结构
  struct inode *ip;  // 如果是inode文件或设备文件，指向inode结构
  uint off;          // 文件偏移量
  short major;       // 主设备号
};

#define major(dev) ((dev) >> 16 & 0xFFFF)     // 获取主设备号
#define minor(dev) ((dev) & 0xFFFF)           // 获取次设备号
#define mkdev(m, n) ((uint)((m) << 16 | (n))) // 创建设备号

// in-memory copy of an inode
// inode在内存中的副本
struct inode
{
  uint dev;              // 设备号
  uint inum;             // Inode号
  int ref;               // 引用计数
  struct sleeplock lock; // 保护以下所有内容的睡眠锁
  int valid;             // inode是否已从磁盘读取？

  short type;              // 磁盘inode的副本
  short major;             // 主设备号
  short minor;             // 次设备号
  short nlink;             // 硬链接数
  uint size;               // 文件大小
  uint addrs[NDIRECT + 1]; // 数据块地址
};

// map major device number to device functions.
// 将主设备号映射到设备函数。
struct devsw
{
  int (*read)(int, uint64, int);
  int (*write)(int, uint64, int);
};

extern struct devsw devsw[];

#define CONSOLE 1 // 控制台设备号