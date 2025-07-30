#define NPROC        64  // 最大进程数
#define NCPU          8  // 最大 CPU 核数
#define NOFILE       16  // 每个进程的最大打开文件数
#define NFILE       100  // 系统范围内的最大打开文件数
#define NINODE       50  // 最大活动 i-node 数
#define NDEV         10  // 最大主设备号
#define ROOTDEV       1  // 文件系统根磁盘的设备号
#define MAXARG       32  // exec 的最大参数数量
#define MAXOPBLOCKS  10  // 任何文件系统操作写入的最大块数
#define LOGSIZE      (MAXOPBLOCKS*3)  // 磁盘日志中最大数据块数
#define NBUF         (MAXOPBLOCKS*3)  // 磁盘块缓存的大小
#define FSSIZE       2000  // 文件系统的大小 (块)
#define MAXPATH      128   // 最大文件路径名长度
#define USERSTACK    1     // 用户栈的页数

