#define NPROC        64  // 系统支持的最大进程数量
#define NCPU          8  // 系统支持的最大 CPU 核心数量
#define NOFILE       16  // 每个进程可以打开的最大文件描述符数量
#define NFILE       100  // 整个系统可以打开的最大文件数量
#define NINODE       50  // 系统中最大活跃的 i-node 数量
#define NDEV         10  // 系统支持的最大主设备号
#define ROOTDEV       1  // 根文件系统所在的设备号 (1 表示 virtio_disk)
#define MAXARG       32  // exec 系统调用允许的最大参数数量
#define MAXOPBLOCKS  10  // 任何单个文件系统操作（如 write）写入的最大块数
#define LOGSIZE      (MAXOPBLOCKS*3)  // 日志区域的大小（以块为单位）
#define NBUF         (MAXOPBLOCKS*3)  // 磁盘块缓存 (buffer cache) 的大小，与日志大小相同
#define FSSIZE       2000  // 文件系统的大小（以块为单位）
#define MAXPATH      128   // 文件路径的最大长度
#define USERSTACK    1     // 用户栈的大小（以页为单位）
