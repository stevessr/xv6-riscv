struct buf;           // 缓冲区头结构体
struct context;       // 进程上下文结构体
struct file;          // 文件结构体
struct inode;         // Inode 结构体
struct pipe;          // 管道结构体
struct proc;          // 进程控制块结构体
struct spinlock;      // 自旋锁结构体
struct sleeplock;     // 睡眠锁结构体
struct stat;          // 文件状态结构体
struct superblock;    // 超级块结构体

// bio.c - 块设备接口
void            binit(void);      // 初始化缓冲区缓存
struct buf*     bread(uint, uint); // 读取一个块到缓冲区
void            brelse(struct buf*); // 释放一个缓冲区
void            bwrite(struct buf*); // 写一个缓冲区到磁盘
void            bpin(struct buf*);   // 钉住一个缓冲区，增加其引用计数
void            bunpin(struct buf*); // 取消钉住一个缓冲区，减少其引用计数

// console.c - 控制台驱动
void            consoleinit(void); // 初始化控制台设备
void            consoleintr(int);  // 控制台中断处理函数
void            consputc(int);   // 向控制台输出一个字符

// exec.c - 执行文件
int             exec(char*, char**); // 加载并执行一个程序

// file.c - 文件系统接口
struct file*    filealloc(void); // 分配一个文件结构体
void            fileclose(struct file*); // 关闭一个文件
struct file*    filedup(struct file*); // 复制一个文件描述符
void            fileinit(void); // 初始化文件表
int             fileread(struct file*, uint64, int n); // 从文件中读取数据
int             filestat(struct file*, uint64 addr); // 获取文件状态
int             filewrite(struct file*, uint64, int n); // 向文件中写入数据

// fs.c - 文件系统
void            fsinit(int);      // 初始化文件系统
int             dirlink(struct inode*, char*, uint); // 创建一个目录项链接
struct inode*   dirlookup(struct inode*, char*, uint*); // 在目录中查找一个 inode
struct inode*   ialloc(uint, short); // 分配一个 inode
struct inode*   idup(struct inode*); // 复制一个 inode
void            iinit();          // 初始化 inode 缓存
void            ilock(struct inode*); // 锁定一个 inode
void            iput(struct inode*); // 释放一个 inode
void            iunlock(struct inode*); // 解锁一个 inode
void            iunlockput(struct inode*); // 解锁并释放一个 inode
void            iupdate(struct inode*); // 将 inode 的内容写回磁盘
int             namecmp(const char*, const char*); // 比较两个路径名
struct inode*   namei(char*);     // 将路径名转换为 inode
struct inode*   nameiparent(char*, char*); // 获取父目录的 inode 和最后一个路径分量
int             readi(struct inode*, int, uint64, uint, uint); // 从 inode 读取数据
void            stati(struct inode*, struct stat*); // 获取 inode 的状态
int             writei(struct inode*, int, uint64, uint, uint); // 向 inode 写入数据
void            itrunc(struct inode*); // 截断一个 inode

// ramdisk.c - 内存虚拟磁盘
void            ramdiskinit(void); // 初始化内存盘
void            ramdiskintr(void); // 内存盘中断处理函数
void            ramdiskrw(struct buf*); // 读写内存盘

// kalloc.c - 物理内存分配器
void*           kalloc(void);     // 分配一页物理内存
void            kfree(void *);    // 释放一页物理内存
void            kinit(void);      // 初始化物理内存分配器

// log.c - 日志
void            initlog(int, struct superblock*); // 初始化日志系统
void            log_write(struct buf*); // 将一个缓冲区写入日志
void            begin_op(void);   // 开始一个文件系统操作
void            end_op(void);     // 结束一个文件系统操作

// pipe.c - 管道
int             pipealloc(struct file**, struct file**); // 分配一个管道
void            pipeclose(struct pipe*, int); // 关闭管道的一端
int             piperead(struct pipe*, uint64, int); // 从管道读取数据
int             pipewrite(struct pipe*, uint64, int); // 向管道写入数据

// printf.c - 格式化输出
int            printf(char*, ...) __attribute__ ((format (printf, 1, 2))); // 内核的格式化输出函数
void            panic(char*) __attribute__((noreturn)); // 发生严重错误时调用，停止系统
void            printfinit(void); // 初始化 printf

// proc.c - 进程
int             cpuid(void);      // 获取当前 CPU 的 ID
void            exit(int);        // 终止当前进程
int             fork(void);       // 创建一个新进程
int             growproc(int);    // 增加或减少进程的内存
void            proc_mapstacks(pagetable_t); // 为进程映射内核栈
pagetable_t     proc_pagetable(struct proc *); // 获取进程的页表
void            proc_freepagetable(pagetable_t, uint64); // 释放进程的页表
int             kill(int);        // 向指定进程发送信号，杀死它
int             killed(struct proc*); // 检查进程是否被标记为杀死
void            setkilled(struct proc*); // 标记一个进程为被杀死
struct cpu*     mycpu(void);      // 获取当前 CPU 的结构体指针（中断关闭时使用）
struct cpu*     getmycpu(void);   // 获取当前 CPU 的结构体指针（中断可能开启）
struct proc*    myproc();         // 获取当前进程的结构体指针
void            procinit(void);   // 初始化进程表
void            scheduler(void) __attribute__((noreturn)); // CPU 的调度循环
void            sched(void);      // 让出 CPU，进行一次调度
void            sleep(void*, struct spinlock*); // 使进程进入睡眠状态
void            userinit(void);   // 初始化第一个用户进程
int             wait(uint64);     // 等待子进程退出并获取其状态
void            wakeup(void*);    // 唤醒在某个通道上睡眠的所有进程
void            yield(void);      // 主动让出 CPU
int             either_copyout(int user_dst, uint64 dst, void *src, uint64 len); // 从内核复制数据到用户空间或内核空间
int             either_copyin(void *dst, int user_src, uint64 src, uint64 len);  // 从用户空间或内核空间复制数据到内核
void            procdump(void);   // 打印进程列表以用于调试

// swtch.S - 上下文切换
void            swtch(struct context*, struct context*); // 切换上下文

// spinlock.c - 自旋锁
void            acquire(struct spinlock*); // 获取一个自旋锁
int             holding(struct spinlock*); // 检查当前 CPU 是否持有该锁
void            initlock(struct spinlock*, char*); // 初始化一个自旋锁
void            release(struct spinlock*); // 释放一个自旋锁
void            push_off(void);    // 压入中断关闭状态
void            pop_off(void);     // 弹出中断状态

// sleeplock.c - 睡眠锁
void            acquiresleep(struct sleeplock*); // 获取一个睡眠锁
void            releasesleep(struct sleeplock*); // 释放一个睡眠锁
int             holdingsleep(struct sleeplock*); // 检查是否持有该睡眠锁
void            initsleeplock(struct sleeplock*, char*); // 初始化一个睡眠锁

// string.c - 字符串操作
int             memcmp(const void*, const void*, uint); // 比较两块内存区域
void*           memmove(void*, const void*, uint); // 移动内存区域
void*           memset(void*, int, uint); // 填充内存区域
char*           safestrcpy(char*, const char*, int); // 安全地复制字符串
int             strlen(const char*); // 计算字符串长度
int             strncmp(const char*, const char*, uint); // 比较两个字符串的前n个字节
char*           strncpy(char*, const char*, int); // 复制字符串的前n个字节

// syscall.c - 系统调用
void            argint(int, int*); // 获取第 n 个整型系统调用参数
int             argstr(int, char*, int); // 获取第 n 个字符串系统调用参数
void            argaddr(int, uint64 *); // 获取第 n 个地址系统调用参数
int             fetchstr(uint64, char*, int); // 从用户空间获取一个字符串
int             fetchaddr(uint64, uint64*); // 从用户空间获取一个地址
void            syscall();        // 系统调用处理入口

// trap.c - 中断和异常
extern uint     ticks;          // 时钟滴答计数器
void            trapinit(void);   // 初始化中断向量表
void            trapinithart(void); // 每个 CPU 核的中断初始化
extern struct spinlock tickslock; // 保护 ticks 变量的锁
void            usertrapret(void); // 从用户陷阱返回

// uart.c - UART驱动
void            uartinit(void);   // 初始化 UART 设备
void            uartintr(void);   // UART 中断处理函数
void            uartputc(int);    // 通过 UART 发送一个字符（可能需要等待）
void            uartputc_sync(int); // 同步地通过 UART 发送一个字符（忙等待）
int             uartgetc(void);   // 从 UART 接收一个字符

// vm.c - 虚拟内存
void            kvminit(void);    // 初始化内核页表
void            kvminithart(void); // 每个 CPU 核的虚拟内存初始化
void            kvmmap(pagetable_t, uint64, uint64, uint64, int); // 在内核页表中映射物理内存
int             mappages(pagetable_t, uint64, uint64, uint64, int); // 在页表中创建映射
pagetable_t     uvmcreate(void);  // 创建一个空的用户页表
void            uvmfirst(pagetable_t, uchar *, uint); // 加载第一个用户进程
uint64          uvmalloc(pagetable_t, uint64, uint64, int); // 在用户页表中分配内存
uint64          uvmdealloc(pagetable_t, uint64, uint64); // 在用户页表中释放内存
int             uvmcopy(pagetable_t, pagetable_t, uint64); // 复制一个用户页表
void            uvmfree(pagetable_t, uint64); // 释放一个用户页表
void            uvmunmap(pagetable_t, uint64, uint64, int); // 取消用户页表中的映射
void            uvmclear(pagetable_t, uint64); // 清除页表项的 U 位
pte_t *         walk(pagetable_t, uint64, int); // 遍历页表找到一个虚拟地址对应的PTE
uint64          walkaddr(pagetable_t, uint64); // 找到一个虚拟地址对应的物理地址
int             copyout(pagetable_t, uint64, char *, uint64); // 从内核复制数据到用户空间
int             copyin(pagetable_t, char *, uint64, uint64); // 从用户空间复制数据到内核
int             copyinstr(pagetable_t, char *, uint64, uint64); // 从用户空间复制字符串到内核

// plic.c - 平台级中断控制器
void            plicinit(void);   // 初始化 PLIC
void            plicinithart(void); // 每个 CPU 核的 PLIC 初始化
int             plic_claim(void); // 获取一个挂起的中断
void            plic_complete(int); // 通知 PLIC 中断已处理

// virtio_disk.c - Virtio磁盘驱动
void            virtio_disk_init(void); // 初始化 virtio 磁盘设备
void            virtio_disk_rw(struct buf *, int); // 读写 virtio 磁盘
void            virtio_disk_intr(void); // virtio 磁盘中断处理函数

// 系统关机函数
void            shutdown(void); // 关闭系统
void            reboot(void);   // 重启系统

// 计算定长数组元素个数
#define NELEM(x) (sizeof(x)/sizeof((x)[0])) // 计算一个静态数组的元素个数
