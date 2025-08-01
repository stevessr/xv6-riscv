// On-disk file system format.
// Both the kernel and user programs use this header file.
// 磁盘上的文件系统格式。
// 内核和用户程序都使用这个头文件来理解文件系统的结构。

#define ROOTINO 1  // root i-number
                   // 定义根目录的 inode 编号，固定为 1。

#define BSIZE 1024 // block size
                   // 定义文件系统中每个块（block）的大小，单位为字节。

// Disk layout:
// [ boot block | super block | log | inode blocks | free bit map | data blocks ]
// 磁盘布局:
// [ 引导块 | 超级块 | 日志区 | inode 块 | 空闲位图 | 数据块 ]
//
// mkfs computes the super block and builds an initial file system. The
// super block describes the disk layout:
// mkfs 程序会计算超级块并构建初始的文件系统。
// 超级块描述了整个文件系统的元数据信息:
struct superblock {
  uint magic;        // Must be FSMAGIC
                     // 文件系统幻数，必须等于 FSMAGIC，用于标识这是一个有效的 xv6 文件系统。
  uint size;         // Size of file system image (blocks)
                     // 文件系统的总大小，以块（BSIZE）为单位。
  uint nblocks;      // Number of data blocks
                     // 数据块的总数。
  uint ninodes;      // Number of inodes.
                     // inode 的总数。
  uint nlog;         // Number of log blocks
                     // 日志区域所占用的块数。
  uint logstart;     // Block number of first log block
                     // 日志区域的起始块号。
  uint inodestart;   // Block number of first inode block
                     // inode 区域的起始块号。
  uint bmapstart;    // Block number of first free map block
                     // 空闲块位图（bitmap）的起始块号。
};

#define FSMAGIC 0x10203040 // 文件系统幻数，用于在读取超级块时进行识别和验证。

#define NDIRECT 12         // 每个 inode 中直接数据块指针的数量。
#define NINDIRECT (BSIZE / sizeof(uint)) // 每个间接块中可以存储的数据块指针的数量。
#define MAXFILE (NDIRECT + NINDIRECT)      // 单个文件可以拥有的最大数据块数量（直接块 + 间接块）。

// On-disk inode structure
// 存储在磁盘上的 inode 结构。
struct dinode {
  short type;           // File type
                        // 文件类型：0-未分配, 1-目录(T_DIR), 2-文件(T_FILE), 3-设备(T_DEVICE)。
  short major;          // Major device number (T_DEVICE only)
                        // 主设备号，仅当文件类型为设备（T_DEVICE）时有效。
  short minor;          // Minor device number (T_DEVICE only)
                        // 次设备号，仅当文件类型为设备（T_DEVICE）时有效。
  short nlink;          // Number of links to inode in file system
                        // 指向此 inode 的硬链接数量。当 nlink 降为 0 时，inode 及其数据块将被回收。
  uint size;            // Size of file (bytes)
                        // 文件的大小，以字节为单位。
  uint addrs[NDIRECT+1];   // Data block addresses
                        // 数据块地址数组。前 NDIRECT 个是直接数据块的地址，最后一个指向间接数据块的地址。
};

// Inodes per block.
// 计算每个块可以存储多少个磁盘 inode (dinode)。
#define IPB           (BSIZE / sizeof(struct dinode))

// Block containing inode i
// 根据 inode 编号 i 和超级块 sb，计算出该 inode 所在的块号。
#define IBLOCK(i, sb)     ((i) / IPB + sb.inodestart)

// Bitmap bits per block
// 计算每个位图块所包含的位数。每个位代表一个数据块的空闲状态（0表示空闲，1表示已分配）。
#define BPB           (BSIZE*8)

// Block of free map containing bit for block b
// 根据数据块号 b 和超级块 sb，计算出管理该块空闲状态的位图所在的块号。
#define BBLOCK(b, sb) ((b)/BPB + sb.bmapstart)

// Directory is a file containing a sequence of dirent structures.
// 目录是一种特殊的文件，其内容是一系列 dirent (目录项) 结构。
#define DIRSIZ 14 // 定义目录项中文件名的最大长度。

// 目录项结构
struct dirent {
  ushort inum;      // 目录项对应的 inode 编号。如果为 0，表示该目录项是空闲的。
  char name[DIRSIZ]; // 目录项的文件名。
};
