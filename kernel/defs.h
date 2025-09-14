struct buf;
struct context;
struct file;
struct inode;
struct pipe;
struct proc;
struct spinlock;
struct sleeplock;
struct stat;
struct superblock;

// bio.c
void binit(void);              // 初始化缓冲区缓存
struct buf *bread(uint, uint); // 读取一个块到缓冲区
void brelse(struct buf *);     // 释放一个缓冲区
void bwrite(struct buf *);     // 写一个缓冲区
void bpin(struct buf *);       // 钉住一个缓冲区
void bunpin(struct buf *);     // 取消钉住一个缓冲区

// console.c
void consoleinit(void); // 初始化控制台
void consoleintr(int);  // 控制台中断处理
void consputc(int);     // 控制台输出一个字符

// exec.c
int kexec(char *, char **); // 执行一个程序

// file.c
struct file *filealloc(void);                // 分配一个文件结构
void fileclose(struct file *);               // 关闭一个文件
struct file *filedup(struct file *);         // 复制一个文件
void fileinit(void);                         // 初始化文件表
int fileread(struct file *, uint64, int n);  // 读文件
int filestat(struct file *, uint64 addr);    // 获取文件状态
int filewrite(struct file *, uint64, int n); // 写文件

// fs.c
void fsinit(int);                                        // 初始化文件系统
int dirlink(struct inode *, char *, uint);               // 创建目录链接
struct inode *dirlookup(struct inode *, char *, uint *); // 在目录中查找
struct inode *ialloc(uint, short);                       // 分配一个inode
struct inode *idup(struct inode *);                      // 复制一个inode
void iinit();                                            // 初始化inode缓存
void ilock(struct inode *);                              // 锁定一个inode
void iput(struct inode *);                               // 释放一个inode
void iunlock(struct inode *);                            // 解锁一个inode
void iunlockput(struct inode *);                         // 解锁并释放一个inode
void iupdate(struct inode *);                            // 更新一个inode
int namecmp(const char *, const char *);                 // 比较两个名字
struct inode *namei(char *);                             // 路径名转换为inode
struct inode *nameiparent(char *, char *);               // 父路径名转换为inode
int readi(struct inode *, int, uint64, uint, uint);      // 读inode
void stati(struct inode *, struct stat *);               // 获取inode状态
int writei(struct inode *, int, uint64, uint, uint);     // 写inode
void itrunc(struct inode *);                             // 截断inode
void ireclaim(int);                                      // 回收inode

// kalloc.c
void *kalloc(void); // 分配一页物理内存
void kfree(void *); // 释放一页物理内存
void kinit(void);   // 初始化物理内存分配器

// log.c
void initlog(int, struct superblock *); // 初始化日志
void log_write(struct buf *);           // 写日志
void begin_op(void);                    // 开始一个文件系统操作
void end_op(void);                      // 结束一个文件系统操作

// pipe.c
int pipealloc(struct file **, struct file **); // 分配一个管道
void pipeclose(struct pipe *, int);            // 关闭一个管道
int piperead(struct pipe *, uint64, int);      // 读管道
int pipewrite(struct pipe *, uint64, int);     // 写管道

// printf.c
int printf(char *, ...) __attribute__((format(printf, 1, 2))); // 格式化输出
void panic(char *) __attribute__((noreturn));                  // 系统崩溃
void printfinit(void);                                         // 初始化printf

// proc.c
int cpuid(void);                                                     // 获取CPU ID
void kexit(int);                                                     // 退出当前进程
int kfork(void);                                                     // 创建一个新进程
int growproc(int);                                                   // 增长或收缩进程内存
void proc_mapstacks(pagetable_t);                                    // 映射进程栈
pagetable_t proc_pagetable(struct proc *);                           // 获取进程页表
void proc_freepagetable(pagetable_t, uint64);                        // 释放进程页表
int kkill(int);                                                      // 杀死一个进程
int killed(struct proc *);                                           // 检查进程是否被杀死
void setkilled(struct proc *);                                       // 设置进程为被杀死状态
struct cpu *mycpu(void);                                             // 获取当前CPU
struct proc *myproc();                                               // 获取当前进程
void procinit(void);                                                 // 初始化进程表
void scheduler(void) __attribute__((noreturn));                      // 调度器
void sched(void);                                                    // 切换到调度器
void sleep(void *, struct spinlock *);                               // 睡眠
void userinit(void);                                                 // 初始化第一个用户进程
int kwait(uint64);                                                   // 等待子进程退出
void wakeup(void *);                                                 // 唤醒睡眠的进程
void yield(void);                                                    // 放弃CPU
int either_copyout(int user_dst, uint64 dst, void *src, uint64 len); // 内核到用户或内核的拷贝
int either_copyin(void *dst, int user_src, uint64 src, uint64 len);  // 用户或内核到内核的拷贝
void procdump(void);                                                 // 打印进程列表

// swtch.S
void swtch(struct context *, struct context *); // 上下文切换

// spinlock.c
void acquire(struct spinlock *);          // 获取自旋锁
int holding(struct spinlock *);           // 检查是否持有自旋锁
void initlock(struct spinlock *, char *); // 初始化自旋锁
void release(struct spinlock *);          // 释放自旋锁
void push_off(void);                      // 禁用中断
void pop_off(void);                       // 启用中断

// sleeplock.c
void acquiresleep(struct sleeplock *);          // 获取睡眠锁
void releasesleep(struct sleeplock *);          // 释放睡眠锁
int holdingsleep(struct sleeplock *);           // 检查是否持有睡眠锁
void initsleeplock(struct sleeplock *, char *); // 初始化睡眠锁

// string.c
int memcmp(const void *, const void *, uint);  // 比较内存区域
void *memmove(void *, const void *, uint);     // 移动内存区域
void *memset(void *, int, uint);               // 设置内存区域
char *safestrcpy(char *, const char *, int);   // 安全的字符串拷贝
int strlen(const char *);                      // 计算字符串长度
int strncmp(const char *, const char *, uint); // 比较字符串
char *strncpy(char *, const char *, int);      // 拷贝字符串

// syscall.c
void argint(int, int *);           // 获取整型系统调用参数
int argstr(int, char *, int);      // 获取字符串系统调用参数
void argaddr(int, uint64 *);       // 获取地址系统调用参数
int fetchstr(uint64, char *, int); // 从用户空间获取字符串
int fetchaddr(uint64, uint64 *);   // 从用户空间获取地址
void syscall();                    // 系统调用处理

// trap.c
extern uint ticks;                // 时钟中断计数
void trapinit(void);              // 初始化中断
void trapinithart(void);          // 初始化hart的中断
extern struct spinlock tickslock; // 保护ticks的锁
void prepare_return(void);        // 准备返回用户空间

// uart.c
void uartinit(void);         // 初始化UART
void uartintr(void);         // UART中断处理
void uartwrite(char[], int); // UART写
void uartputc_sync(int);     // 同步UART输出一个字符
int uartgetc(void);          // 从UART获取一个字符

// vm.c
void kvminit(void);                                     // 初始化内核页表
void kvminithart(void);                                 // 初始化hart的页表
void kvmmap(pagetable_t, uint64, uint64, uint64, int);  // 映射内核虚拟地址
int mappages(pagetable_t, uint64, uint64, uint64, int); // 映射物理页
pagetable_t uvmcreate(void);                            // 创建用户页表
uint64 uvmalloc(pagetable_t, uint64, uint64, int);      // 分配用户虚拟内存
uint64 uvmdealloc(pagetable_t, uint64, uint64);         // 释放用户虚拟内存
int uvmcopy(pagetable_t, pagetable_t, uint64);          // 拷贝用户虚拟内存
void uvmfree(pagetable_t, uint64);                      // 释放用户页表
void uvmunmap(pagetable_t, uint64, uint64, int);        // 取消用户虚拟地址映射
void uvmclear(pagetable_t, uint64);                     // 清除用户页表项的U位
pte_t *walk(pagetable_t, uint64, int);                  // 遍历页表
uint64 walkaddr(pagetable_t, uint64);                   // 获取虚拟地址对应的物理地址
int copyout(pagetable_t, uint64, char *, uint64);       // 从内核拷贝到用户空间
int copyin(pagetable_t, char *, uint64, uint64);        // 从用户空间拷贝到内核
int copyinstr(pagetable_t, char *, uint64, uint64);     // 从用户空间拷贝字符串到内核
int ismapped(pagetable_t, uint64);                      // 检查虚拟地址是否映射
uint64 vmfault(pagetable_t, uint64, int);               // 缺页中断处理

// plic.c
void plicinit(void);     // 初始化PLIC
void plicinithart(void); // 初始化hart的PLIC
int plic_claim(void);    // 获取中断
void plic_complete(int); // 完成中断处理

// virtio_disk.c
void virtio_disk_init(void);            // 初始化virtio磁盘
void virtio_disk_rw(struct buf *, int); // 读写virtio磁盘
void virtio_disk_intr(void);            // virtio磁盘中断处理

// number of elements in fixed-size array
// 计算定长数组的元素个数
#define NELEM(x) (sizeof(x) / sizeof((x)[0]))
