// System call numbers
// 系统调用号定义

// fork() 创建一个与当前进程完全相同的子进程。
// 子进程拥有父进程内存空间的副本，并继承父进程打开的文件描述符。
// 成功时，在父进程中返回子进程的PID，在子进程中返回0。失败时返回-1。
#define SYS_fork     1

// exit(status) 终止当前进程，并将其退出状态 `status` 返回给父进程。
// 该进程占用的所有资源将被释放。
#define SYS_exit     2

// wait(status_ptr) 等待一个子进程退出，并获取其退出状态。
// 如果没有子进程退出，则会阻塞当前进程，直到有子进程退出。
// 成功时返回退出的子进程PID，并将其退出状态写入 `status_ptr` 指向的地址。
#define SYS_wait     3

// pipe(fd_array) 创建一个管道，用于进程间通信。
// `fd_array` 是一个包含两个整数的数组，`fd_array[0]` 用于读，`fd_array[1]` 用于写。
// 成功时返回0，失败时返回-1。
#define SYS_pipe     4

// read(fd, buf, n) 从文件描述符 `fd` 中读取最多 `n` 个字节到缓冲区 `buf`。
// 返回实际读取的字节数，如果已到文件末尾则返回0，失败时返回-1。
#define SYS_read     5

// kill(pid) 向进程ID为 `pid` 的进程发送一个终止信号。
// 成功时返回0，失败时返回-1（例如，进程不存在或权限不足）。
#define SYS_kill     6

// exec(path, argv) 加载并执行路径为 `path` 的可执行文件，替换当前进程的内存映像。
// `argv` 是传递给新程序的命令行参数数组。
// 成功时不会返回，失败时返回-1。
#define SYS_exec     7

// fstat(fd, stat_buf) 获取文件描述符 `fd` 指向的文件的元数据信息。
// `stat_buf` 是一个指向 `struct stat` 的指针，用于存储文件状态。
// 成功时返回0，失败时返回-1。
#define SYS_fstat    8

// chdir(path) 将当前进程的工作目录更改为 `path`。
// 成功时返回0，失败时返回-1。
#define SYS_chdir    9

// dup(fd) 复制一个现有的文件描述符 `fd`。
// 返回一个新的文件描述符，它与旧的 `fd` 指向同一个文件表项。
// 失败时返回-1。
#define SYS_dup     10

// getpid() 返回当前进程的进程ID (PID)。
#define SYS_getpid  11

// sbrk(n) 将进程的数据段大小增加 `n` 字节。
// `n` 可以是负数，表示收缩内存。
// 返回新分配内存的起始地址（即旧的程序中断点）。
#define SYS_sbrk    12

// sleep(ticks) 使当前进程暂停执行 `ticks` 个时钟周期。
// 在指定的 `ticks` 时间后，进程将被唤醒并重新调度。
#define SYS_sleep   13

// uptime() 返回系统自启动以来经过的时钟周期数。
#define SYS_uptime  14

// open(path, flags) 打开路径为 `path` 的文件，并返回一个新的文件描述符。
// `flags` 参数指定文件的打开模式（如O_RDONLY, O_WRONLY, O_CREATE等）。
// 成功时返回文件描述符，失败时返回-1。
#define SYS_open    15

// write(fd, buf, n) 将缓冲区 `buf` 中的 `n` 个字节写入文件描述符 `fd`。
// 返回实际写入的字节数，失败时返回-1。
#define SYS_write   16

// mknod(path, major, minor) 创建一个特殊文件（设备文件）。
// `path` 是文件名，`major` 和 `minor` 是设备号。
// 成功时返回0，失败时返回-1。
#define SYS_mknod   17

// unlink(path) 从文件系统中删除路径为 `path` 的文件。
// 如果文件的链接计数降为0，并且没有进程打开它，则会释放其占用的空间。
// 成功时返回0，失败时返回-1。
#define SYS_unlink  18

// link(old_path, new_path) 为 `old_path` 创建一个新的硬链接 `new_path`。
// 硬链接是现有文件的另一个名字，它们共享相同的inode。
// 成功时返回0，失败时返回-1。
#define SYS_link    19

// mkdir(path) 创建一个名为 `path` 的新目录。
// 成功时返回0，失败时返回-1。
#define SYS_mkdir   20

// close(fd) 关闭文件描述符 `fd`。
// 释放该文件描述符，如果这是最后一个指向该文件的引用，则会释放相关资源。
// 成功时返回0，失败时返回-1。
#define SYS_close   21

// shutdown() 关闭系统。这是一个xv6特有的系统调用，用于测试或关闭虚拟机。
#define SYS_shutdown 22
// reboot() 重启系统
#define SYS_reboot 23
