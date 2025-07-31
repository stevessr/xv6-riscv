typedef unsigned int uint;
struct stat;

// 系统调用
int fork(void); // 创建一个子进程
int exit(int) __attribute__((noreturn)); // 终止当前进程
int wait(int*); // 等待一个子进程退出
int pipe(int*); // 创建一个管道
int write(int, const void*, int); // 写入文件描述符
int read(int, void*, int); // 从文件描述符读取
int close(int); // 关闭文件描述符
int kill(int); // 终止一个进程
int exec(const char*, char**); // 执行一个新程序
int open(const char*, int); // 打开一个文件
int mknod(const char*, short, short); // 创建一个设备文件
int unlink(const char*); // 删除一个文件
int fstat(int fd, struct stat*); // 获取文件状态
int link(const char*, const char*); // 创建一个硬链接
int mkdir(const char*); // 创建一个目录
int chdir(const char*); // 切换当前目录
int dup(int); // 复制一个文件描述符
int getpid(void); // 获取当前进程ID
char* sbrk(int); // 增长进程内存
int sleep(int); // 暂停指定的ticks
int uptime(void); // 获取系统自启动以来的ticks
int shutdown(void); // 关闭系统

// ulib.c
int stat(const char*, struct stat*); // 获取文件状态
char* strcpy(char*, const char*); // 复制字符串
void *memmove(void*, const void*, int); // 移动内存
char* strchr(const char*, char c); // 在字符串中查找字符
int strcmp(const char*, const char*); // 比较字符串
void fprintf(int, const char*, ...) __attribute__ ((format (printf, 2, 3))); // 格式化输出到文件
void printf(const char*, ...) __attribute__ ((format (printf, 1, 2))); // 格式化输出到控制台
char* gets(char*, int max); // 从控制台读取一行
uint strlen(const char*); // 获取字符串长度
void* memset(void*, int, uint); // 填充内存
int atoi(const char*); // 字符串转整数
int memcmp(const void *, const void *, uint); // 比较内存区域
void *memcpy(void *, const void *, uint); // 复制内存区域

// umalloc.c
void* malloc(uint); // 分配内存
void free(void*); // 释放内存
