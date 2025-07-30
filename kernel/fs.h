// 磁盘文件系统格式。
// 内核和用户程序都使用此头文件。


#define ROOTINO  1   // 根 inode 编号
#define BSIZE 1024  // 块大小

// 磁盘布局:
// [ 引导块 | 超级块 | 日志 | inode 块 |
//                                          空闲位图 | 数据块 ]
//
// mkfs 计算超级块并构建初始文件系统。
// 超级块描述了磁盘布局：
struct superblock {
  uint magic;        // 必须是 FSMAGIC
  uint size;         // 文件系统镜像的大小 (块数)
  uint nblocks;      // 数据块的数量
  uint ninodes;      // inode 的数量
  uint nlog;         // 日志块的数量
  uint logstart;     // 第一个日志块的块号
  uint inodestart;   // 第一个 inode 块的块号
  uint bmapstart;    // 第一个空闲位图块的块号
};

#define FSMAGIC 0x10203040 // 文件系统幻数

#define NDIRECT 12 // 直接数据块指针数量
#define NINDIRECT (BSIZE / sizeof(uint)) // 间接数据块指针数量
#define MAXFILE (NDIRECT + NINDIRECT) // 一个文件的最大块数

// 磁盘上的 inode 结构
struct dinode {
  short type;           // 文件类型 (T_DIR, T_FILE, T_DEVICE)
  short major;          // 主设备号 (仅当 type == T_DEVICE)
  short minor;          // 次设备号 (仅当 type == T_DEVICE)
  short nlink;          // 连接到此 inode 的链接数
  uint size;            // 文件大小 (字节)
  uint addrs[NDIRECT+1];   // 数据块地址 (直接 + 1个间接)
};

// 每块中的 Inode 数量
#define IPB           (BSIZE / sizeof(struct dinode))

// inode i 所在的块
#define IBLOCK(i, sb)     ((i) / IPB + sb.inodestart)

// 每块中的位图位数
#define BPB           (BSIZE*8)

// 包含块 b 的空闲位图块
#define BBLOCK(b, sb) ((b)/BPB + sb.bmapstart)

// 目录是一个包含一系列 dirent 结构的文件。
#define DIRSIZ 14

struct dirent {
  ushort inum;      // Inode 编号
  char name[DIRSIZ]; // 目录项名称
};

