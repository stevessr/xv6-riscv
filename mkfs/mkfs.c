// 引入标准输入输出库，用于 printf, fprintf 等函数
#include <stdio.h>
// 引入 Unix 标准库，用于 read, write, lseek, close 等函数
#include <unistd.h>
// 引入标准库，用于 exit, malloc, free 等函数
#include <stdlib.h>
// 引入字符串处理库，用于 memcpy, memset, strcmp, strcpy 等函数
#include <string.h>
// 引入文件控制库，用于 open 等函数，以及 O_RDWR, O_CREAT 等常量
#include <fcntl.h>
// 引入断言库，用于 assert 宏，在调试时验证条件
#include <assert.h>

// 为了避免与主机操作系统（例如 Linux）的 stat 结构体冲突，
// 将 xv6 内部使用的 stat 重命名为 xv6_stat。
#define stat xv6_stat
// 引入 xv6 的基本类型定义，如 uint, ushort, uchar 等
#include "kernel/types.h"
// 引入 xv6 文件系统的定义，如 superblock, dinode, FSMAGIC 等
#include "kernel/fs.h"
// 引入 xv6 的 stat 结构体定义
#include "kernel/stat.h"
// 引入 xv6 的系统参数定义，如 FSSIZE, MAXFILE 等
#include "kernel/param.h"

// 如果编译器不支持 static_assert，则定义一个兼容版本。
// C11 标准引入了 static_assert，但旧的编译器可能不支持。
#ifndef static_assert
#define static_assert(a, b) do { switch (0) case 0: case (a): ; } while (0)
#endif

// 定义文件系统中 inode 的总数
#define NINODES 200

// 磁盘布局注释，描述了文件系统在磁盘上的组织方式：
// [ 引导块 | 超级块 | 日志区 | inode 块 | 空闲位图 | 数据块 ]
//  - 引导块 (Boot block): 用于启动系统，通常是第 0 块。
//  - 超级块 (Superblock): 存储文件系统的元数据，如大小、inode 数量等。
//  - 日志区 (Log): 用于实现日志功能，保证文件系统操作的原子性。
//  - Inode 块: 存储 inode 结构体，每个 inode 描述一个文件或目录。
//  - 空闲位图 (Bitmap): 记录哪些数据块是空闲的。
//  - 数据块 (Data blocks): 存储文件和目录的实际内容。

int nbitmap = FSSIZE / BPB + 1;  // 计算位图所需的块数 (FSSIZE / (BSIZE*8) + 1)
int ninodeblocks = NINODES / IPB + 1;  // 计算 inode 表所需的块数 (NINODES / (BSIZE / sizeof(struct dinode)) + 1)
int nlog = LOGSIZE;  // 日志区占用的块数
int nmeta;  // 元数据块的总数（引导块、超级块、日志、inode、位图）
int nblocks;  // 数据块的总数

int fsfd;  // 文件系统镜像文件的文件描述符
struct superblock sb;  // 超级块结构体实例
char zeroes[BSIZE];  // 一个大小为 BSIZE 的全零缓冲区，用于清空磁盘块
uint freeinode = 1;  // 下一个可分配的空闲 inode 编号，从 1 开始（0 表示无效）
uint freeblock;  // 下一个可分配的空闲数据块的起始块号

// 函数前向声明
void balloc(int); // 标记数据块为已使用
void wsect(uint, void*); // 将数据写入指定扇区
void winode(uint, struct dinode*); // 将 inode 写入磁盘
void rinode(uint inum, struct dinode *ip); // 从磁盘读取 inode
void rsect(uint sec, void *buf); // 从指定扇区读取数据
uint ialloc(ushort type); // 分配一个 inode
void iappend(uint inum, void *p, int n); // 向 inode 对应文件追加数据
void die(const char *); // 打印错误并退出

// 将一个 16 位无符号整数转换为 RISC-V 的小端字节序
ushort
xshort(ushort x)
{
  ushort y;
  uchar *a = (uchar*)&y;
  a[0] = x;      // 低字节
  a[1] = x >> 8; // 高字节
  return y;
}

// 将一个 32 位无符号整数转换为 RISC-V 的小端字节序
uint
xint(uint x)
{
  uint y;
  uchar *a = (uchar*)&y;
  a[0] = x;       // 最低字节
  a[1] = x >> 8;
  a[2] = x >> 16;
  a[3] = x >> 24; // 最高字节
  return y;
}

// mkfs 主函数，创建文件系统镜像
int
main(int argc, char *argv[])
{
  int i, cc, fd;
  uint rootino, inum, off;
  struct dirent de; // 目录项结构体
  char buf[BSIZE];   // 缓冲区
  struct dinode din; // 磁盘上的 inode 结构体

  // 静态断言，确保 int 类型是 4 字节，这对于文件系统布局至关重要
  static_assert(sizeof(int) == 4, "Integers must be 4 bytes!");

  // 检查命令行参数
  if(argc < 2){
    fprintf(stderr, "用法: mkfs fs.img files...\n");
    exit(1);
  }

  // 断言，确保一个块可以容纳整数个 inode 和目录项
  assert((BSIZE % sizeof(struct dinode)) == 0);
  assert((BSIZE % sizeof(struct dirent)) == 0);

  // 打开（或创建）文件系统镜像文件
  fsfd = open(argv[1], O_RDWR|O_CREAT|O_TRUNC, 0666);
  if(fsfd < 0)
    die(argv[1]); // 如果失败则退出

  // 计算元数据块和数据块的数量
  // nmeta = 引导块(1) + 超级块(1) + 日志块 + inode块 + 位图块
  nmeta = 2 + nlog + ninodeblocks + nbitmap;
  nblocks = FSSIZE - nmeta; // 数据块数量 = 总大小 - 元数据大小

  // 初始化超级块
  sb.magic = FSMAGIC; // 设置文件系统魔数
  sb.size = xint(FSSIZE); // 文件系统总大小（块数）
  sb.nblocks = xint(nblocks); // 数据块数量
  sb.ninodes = xint(NINODES); // inode 总数
  sb.nlog = xint(nlog); // 日志块数量
  sb.logstart = xint(2); // 日志区起始块号 (0:boot, 1:super)
  sb.inodestart = xint(2 + nlog); // inode 区起始块号
  sb.bmapstart = xint(2 + nlog + ninodeblocks); // 位图区起始块号

  // 打印文件系统布局信息
  printf("nmeta %d (boot, super, log blocks %u inode blocks %u, bitmap blocks %u) blocks %d total %d\n",
         nmeta, nlog, ninodeblocks, nbitmap, nblocks, FSSIZE);

  // 第一个空闲数据块的起始地址是元数据区之后
  freeblock = nmeta;

  // 将整个文件系统镜像文件初始化为零
  for(i = 0; i < FSSIZE; i++)
    wsect(i, zeroes);

  // 将超级块写入磁盘的第 1 块（第 0 块是引导块）
  memset(buf, 0, sizeof(buf)); // 清空缓冲区
  memmove(buf, &sb, sizeof(sb)); // 将超级块内容复制到缓冲区
  wsect(1, buf); // 写入磁盘

  // 分配根目录的 inode
  rootino = ialloc(T_DIR); // T_DIR 表示目录类型
  assert(rootino == ROOTINO); // 确认根目录的 inode 号是 1

  // 创建根目录的 "." (当前目录) 目录项
  bzero(&de, sizeof(de)); // 清零目录项结构体
  de.inum = xshort(rootino); // inode 号指向根目录自身
  strcpy(de.name, "."); // 名称为 "."
  iappend(rootino, &de, sizeof(de)); // 追加到根目录

  // 创建根目录的 ".." (父目录) 目录项
  bzero(&de, sizeof(de));
  de.inum = xshort(rootino); // 父目录 inode 号也指向根目录自身
  strcpy(de.name, "..");
  iappend(rootino, &de, sizeof(de));

  // 遍历命令行中提供的文件，并将它们添加到文件系统中
  for(i = 2; i < argc; i++){
    char *shortname;
    // 如果文件路径以 "user/" 开头，则去掉这个前缀
    if(strncmp(argv[i], "user/", 5) == 0)
      shortname = argv[i] + 5;
    else
      shortname = argv[i];
    
    // 确保文件名中不包含 '/'，即所有文件都直接放在根目录下
    assert(index(shortname, '/') == 0);

    // 打开文件
    if((fd = open(argv[i], 0)) < 0)
      die(argv[i]);

    // xv6 用户程序在 Makefile 中通常有 '_' 前缀 (如 _cat),
    // 以避免与宿主机的命令冲突。在这里去掉前缀。
    if(shortname[0] == '_')
      shortname++;

    // 确保文件名长度不超过最大限制
    assert(strlen(shortname) <= DIRSIZ);

    // 为文件分配一个新的 inode
    inum = ialloc(T_FILE); // T_FILE 表示普通文件类型

    // 在根目录中为该文件创建一个目录项
    bzero(&de, sizeof(de));
    de.inum = xshort(inum);
    strncpy(de.name, shortname, DIRSIZ);
    iappend(rootino, &de, sizeof(de));

    // 读取文件内容，并将其追加到新创建的 inode 中
    while((cc = read(fd, buf, sizeof(buf))) > 0)
      iappend(inum, buf, cc);

    // 关闭文件
    close(fd);
  }

  // 所有文件都添加完毕后，修正根目录的大小
  rinode(rootino, &din); // 读取根目录 inode
  off = xint(din.size); // 获取当前大小
  off = ((off / BSIZE) + 1) * BSIZE; // 将大小向上取整到最接近的块边界
  din.size = xint(off); // 更新大小
  winode(rootino, &din); // 写回 inode

  // 根据已使用的块数，更新位图
  balloc(freeblock);

  // 正常退出
  exit(0);
}

// 将缓冲区 buf 的内容写入文件系统的指定扇区 sec
void
wsect(uint sec, void *buf)
{
  // 移动文件指针到目标扇区
  if(lseek(fsfd, sec * BSIZE, 0) != sec * BSIZE)
    die("lseek");
  // 写入 BSIZE 字节的数据
  if(write(fsfd, buf, BSIZE) != BSIZE)
    die("write");
}

// 将 inode ip 的内容写入磁盘上编号为 inum 的 inode
void
winode(uint inum, struct dinode *ip)
{
  char buf[BSIZE];
  uint bn;
  struct dinode *dip;

  bn = IBLOCK(inum, sb); // 计算 inode 所在的块号
  rsect(bn, buf); // 读取该块的内容
  dip = ((struct dinode*)buf) + (inum % IPB); // 定位到该块内 specific inode 的位置
  *dip = *ip; // 将新 inode 的内容复制过去
  wsect(bn, buf); // 将修改后的块写回磁盘
}

// 从磁盘读取编号为 inum 的 inode 到 ip
void
rinode(uint inum, struct dinode *ip)
{
  char buf[BSIZE];
  uint bn;
  struct dinode *dip;

  bn = IBLOCK(inum, sb); // 计算 inode 所在的块号
  rsect(bn, buf); // 读取该块
  dip = ((struct dinode*)buf) + (inum % IPB); // 定位到 inode
  *ip = *dip; // 将内容复制到 ip 指向的结构体
}

// 从文件系统的指定扇区 sec 读取数据到缓冲区 buf
void
rsect(uint sec, void *buf)
{
  // 移动文件指针到目标扇区
  if(lseek(fsfd, sec * BSIZE, 0) != sec * BSIZE)
    die("lseek");
  // 读取 BSIZE 字节的数据
  if(read(fsfd, buf, BSIZE) != BSIZE)
    die("read");
}

// 分配一个类型为 type 的新 inode
uint
ialloc(ushort type)
{
  uint inum = freeinode++; // 获取下一个空闲 inode 编号并递增
  struct dinode din;

  bzero(&din, sizeof(din)); // 将 inode 结构体清零
  din.type = xshort(type); // 设置 inode 类型 (文件或目录)
  din.nlink = xshort(1); // 初始化链接数为 1
  din.size = xint(0); // 初始化大小为 0
  winode(inum, &din); // 将初始化后的 inode 写回磁盘
  return inum; // 返回新分配的 inode 编号
}

// 更新数据块位图，标记前 `used` 个块为已分配
void
balloc(int used)
{
  uchar buf[BSIZE];
  int i;

  printf("balloc: first %d blocks have been allocated\n", used);
  assert(used < BPB*BSIZE); // 确保已用块数没有超出位图能表示的范围
  bzero(buf, BSIZE); // 清空位图缓冲区
  // 遍历所有已使用的块 (从元数据区开始)
  for(i = 0; i < used; i++){
    // 在位图中将对应位置的 bit 设为 1
    buf[i/8] = buf[i/8] | (0x1 << (i%8));
  }
  printf("balloc: write bitmap block at sector %d\n", sb.bmapstart);
  // 将更新后的位图写回磁盘
  wsect(xint(sb.bmapstart), buf);
}

// 返回 a 和 b 中的较小值
#define min(a, b) ((a) < (b) ? (a) : (b))

// 向 inode `inum` 对应的文件中追加 `n` 字节的数据，数据源是 `xp`
void
iappend(uint inum, void *xp, int n)
{
  char *p = (char*)xp; // 数据源指针
  uint fbn, off, n1;
  struct dinode din;
  char buf[BSIZE];
  uint indirect[NINDIRECT]; // 间接块缓冲区
  uint x;

  rinode(inum, &din); // 读取 inode 信息
  off = xint(din.size); // 获取当前文件大小作为追加的起始偏移
  
  while(n > 0){
    fbn = off / BSIZE; // 计算当前偏移所在的逻辑块号
    assert(fbn < MAXFILE);

    if(fbn < NDIRECT){ // 直接数据块
      if(xint(din.addrs[fbn]) == 0){ // 如果该直接块指针为空
        din.addrs[fbn] = xint(freeblock++); // 分配一个新的数据块
      }
      x = xint(din.addrs[fbn]); // 获取数据块的物理地址
    } else { // 间接数据块
      if(xint(din.addrs[NDIRECT]) == 0){ // 如果间接块指针为空
        din.addrs[NDIRECT] = xint(freeblock++); // 分配一个新的间接块
      }
      rsect(xint(din.addrs[NDIRECT]), (char*)indirect); // 读取间接块内容
      if(indirect[fbn - NDIRECT] == 0){ // 如果间接块中对应的条目为空
        indirect[fbn - NDIRECT] = xint(freeblock++); // 分配一个新的数据块
        wsect(xint(din.addrs[NDIRECT]), (char*)indirect); // 将更新后的间接块写回
      }
      x = xint(indirect[fbn-NDIRECT]); // 获取数据块的物理地址
    }

    // 计算本次写入可以写入多少字节 (不能超过块的边界)
    n1 = min(n, (fbn + 1) * BSIZE - off);
    rsect(x, buf); // 读取目标数据块
    bcopy(p, buf + off - (fbn * BSIZE), n1); // 将数据复制到块内正确的位置
    wsect(x, buf); // 将修改后的数据块写回

    // 更新循环变量
    n -= n1;   // 剩余待写入字节数
    off += n1; // 更新文件内偏移
    p += n1;   // 更新数据源指针
  }
  din.size = xint(off); // 更新 inode 中的文件大小
  winode(inum, &din); // 将更新后的 inode 写回磁盘
}

// 打印错误信息 `s` 并退出程序
void
die(const char *s)
{
  perror(s);
  exit(1);
}
