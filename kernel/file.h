// 在 xv6 中，文件描述符是一个整数，它作为进程打开文件表的索引。
// 每个进程都维护着自己独立的打开文件表。
// 每个打开的文件（由 struct file 表示）都具有一个类型（例如：管道、inode、设备），
// 如果是 inode 类型，则还会关联一个 i-node，并记录当前的读/写偏移量。
// 此外，每个打开的文件都维护一个引用计数，这对于支持 `fork()` 系统调用（即复制文件描述符）至关重要。
// `struct file` 结构体代表一个已打开的文件。
struct file {
  // 文件类型，可以是以下几种之一：
  // FD_NONE: 未使用
  // FD_PIPE: 管道
  // FD_INODE: 普通文件或目录
  // FD_DEVICE: 设备
  enum { FD_NONE, FD_PIPE, FD_INODE, FD_DEVICE } type;
  int ref;           // 引用计数，记录有多少个文件描述符指向这个文件结构
  char readable;     // 标记文件是否可读 (1 表示可读, 0 表示不可读)
  char writable;     // 标记文件是否可写 (1 表示可写, 0 表示不可读)
  struct pipe *pipe; // 如果 type 是 FD_PIPE，此指针指向关联的管道结构
  struct inode *ip;  // 如果 type 是 FD_INODE 或 FD_DEVICE，此指针指向关联的 inode 结构
  uint off;          // 文件偏移量，仅对 FD_INODE 类型有效，记录下一次读/写的位置
  short major;       // 主设备号，仅对 FD_DEVICE 类型有效
};

// 从一个 32 位的设备号中提取 16 位的major（主）设备号
#define major(dev)  ((dev) >> 16 & 0xFFFF)
// 从一个 32 位的设备号中提取 16 位的minor（次）设备号
#define minor(dev)  ((dev) & 0xFFFF)
// 将 16 位的 major 和 16 位的 minor 设备号合并成一个 32 位的设备号
#define	mkdev(m,n)  ((uint)((m)<<16| (n)))

// inode 在内存中的表示。
// 它与磁盘上的 inode 结构（`struct dinode`）相对应，是磁盘 inode 的一个缓存。
struct inode {
  uint dev;           // 设备号，标识此 inode 所属的文件系统所在的设备
  uint inum;          // Inode 编号，在设备上唯一标识一个 inode
  int ref;            // 引用计数，记录有多少个内存指针指向这个 inode
  struct sleeplock lock; // 睡眠锁，用于保护 inode 的并发访问
  int valid;          // 标记 inode 的内容是否有效（是否已经从磁盘加载）

  // 以下字段是磁盘上 `struct dinode` 的一个拷贝
  short type;         // 文件类型 (T_DIR, T_FILE, T_DEVICE)
  short major;        // 主设备号 (仅当 type == T_DEVICE 时有效)
  short minor;        // 次设备号 (仅当 type == T_DEVICE 时有效)
  short nlink;        // 硬链接数，记录有多少个目录项指向这个 inode
  uint size;          // 文件大小，单位是字节
  uint addrs[NDIRECT+1]; // 数据块地址数组。NDIRECT 是直接块的数量，最后一个是间接块地址。
};

// 设备驱动程序切换表。
// 通过主设备号索引，可以找到对应的读写函数。
struct devsw {
  int (*read)(int, uint64, int);
  int (*write)(int, uint64, int);
};

extern struct devsw devsw[];

#define CONSOLE 1 // 定义控制台设备的主设备号为 1
