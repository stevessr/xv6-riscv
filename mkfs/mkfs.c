#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <assert.h>

#define stat xv6_stat  // 避免与主机上的 stat 结构冲突
#include "kernel/types.h"
#include "kernel/fs.h"
#include "kernel/stat.h"
#include "kernel/param.h"

#ifndef static_assert
#define static_assert(a, b) do { switch (0) case 0: case (a): ; } while (0)
#endif

#define NINODES 200  // inode 的数量

// 磁盘布局:
// [ 引导块 | 超级块 | 日志区 | inode 块 | 空闲位图 | 数据块 ]

int nbitmap = FSSIZE / BPB + 1;  // 位图块的数量
int ninodeblocks = NINODES / IPB + 1;  // inode 块的数量
int nlog = LOGSIZE;  // 日志块的数量
int nmeta;  // 元数据块的数量 (引导块, 超级块, 日志, inode, 位图)
int nblocks;  // 数据块的数量

int fsfd;  // 文件系统镜像的文件描述符
struct superblock sb;  // 超级块
char zeroes[BSIZE];  // 用于清零的缓冲区
uint freeinode = 1;  // 第一个空闲 inode 的编号
uint freeblock;  // 第一个空闲数据块的块号


void balloc(int);
void wsect(uint, void*);
void winode(uint, struct dinode*);
void rinode(uint inum, struct dinode *ip);
void rsect(uint sec, void *buf);
uint ialloc(ushort type);
void iappend(uint inum, void *p, int n);
void die(const char *);

// 转换为 RISC-V 字节序 (小端)
ushort
xshort(ushort x)
{
  ushort y;
  uchar *a = (uchar*)&y;
  a[0] = x;
  a[1] = x >> 8;
  return y;
}

// 转换为 RISC-V 字节序 (小端)
uint
xint(uint x)
{
  uint y;
  uchar *a = (uchar*)&y;
  a[0] = x;
  a[1] = x >> 8;
  a[2] = x >> 16;
  a[3] = x >> 24;
  return y;
}

int
main(int argc, char *argv[])
{
  int i, cc, fd;
  uint rootino, inum, off;
  struct dirent de;
  char buf[BSIZE];
  struct dinode din;


  static_assert(sizeof(int) == 4, "Integers must be 4 bytes!");

  if(argc < 2){
    fprintf(stderr, "用法: mkfs fs.img files...\n");
    exit(1);
  }

  assert((BSIZE % sizeof(struct dinode)) == 0);
  assert((BSIZE % sizeof(struct dirent)) == 0);

  // 打开或创建文件系统镜像文件
  fsfd = open(argv[1], O_RDWR|O_CREAT|O_TRUNC, 0666);
  if(fsfd < 0)
    die(argv[1]);

  // 1 个文件系统块 = 1 个磁盘扇区
  nmeta = 2 + nlog + ninodeblocks + nbitmap; // 计算元数据块总数
  nblocks = FSSIZE - nmeta; // 计算数据块总数

  // 初始化超级块
  sb.magic = FSMAGIC;
  sb.size = xint(FSSIZE);
  sb.nblocks = xint(nblocks);
  sb.ninodes = xint(NINODES);
  sb.nlog = xint(nlog);
  sb.logstart = xint(2);
  sb.inodestart = xint(2 + nlog);
  sb.bmapstart = xint(2 + nlog + ninodeblocks);

  printf("nmeta %d (boot, super, log blocks %u inode blocks %u, bitmap blocks %u) blocks %d total %d\n",
         nmeta, nlog, ninodeblocks, nbitmap, nblocks, FSSIZE);

  freeblock = nmeta;     // 第一个可分配的空闲块

  // 将整个文件系统镜像清零
  for(i = 0; i < FSSIZE; i++)
    wsect(i, zeroes);

  // 将超级块写入第 1 块 (第 0 块是引导块)
  memset(buf, 0, sizeof(buf));
  memmove(buf, &sb, sizeof(sb));
  wsect(1, buf);

  // 分配根目录 inode
  rootino = ialloc(T_DIR);
  assert(rootino == ROOTINO);

  // 创建 "." 目录项
  bzero(&de, sizeof(de));
  de.inum = xshort(rootino);
  strcpy(de.name, ".");
  iappend(rootino, &de, sizeof(de));

  // 创建 ".." 目录项
  bzero(&de, sizeof(de));
  de.inum = xshort(rootino);
  strcpy(de.name, "..");
  iappend(rootino, &de, sizeof(de));

  // 将命令行参数中指定的文件写入文件系统
  for(i = 2; i < argc; i++){
    // 去掉路径前缀 "user/"
    char *shortname;
    if(strncmp(argv[i], "user/", 5) == 0)
      shortname = argv[i] + 5;
    else
      shortname = argv[i];
    
    assert(index(shortname, '/') == 0);

    if((fd = open(argv[i], 0)) < 0)
      die(argv[i]);

    // 跳过文件名前导的 '_'
    // 可执行文件被命名为 _rm, _cat 等，以防止
    // 构建操作系统时尝试在本地执行它们
    // 而不是系统二进制文件 rm 和 cat
    if(shortname[0] == '_')
      shortname += 1;

    assert(strlen(shortname) <= DIRSIZ);
    
    // 为文件分配 inode
    inum = ialloc(T_FILE);

    // 在根目录中创建文件的目录项
    bzero(&de, sizeof(de));
    de.inum = xshort(inum);
    strncpy(de.name, shortname, DIRSIZ);
    iappend(rootino, &de, sizeof(de));

    // 从文件中读取内容并写入文件系统
    while((cc = read(fd, buf, sizeof(buf))) > 0)
      iappend(inum, buf, cc);

    close(fd);
  }

  // 修正根目录 inode 的大小
  rinode(rootino, &din);
  off = xint(din.size);
  off = ((off / BSIZE) + 1) * BSIZE; // 大小向上取整到块边界
  din.size = xint(off);
  winode(rootino, &din);

  // 更新位图，标记已使用的块
  balloc(freeblock);

  exit(0);
}

// 将 buf 中的内容写入扇区 sec
void
wsect(uint sec, void *buf)
{
  if(lseek(fsfd, sec * BSIZE, 0) != sec * BSIZE)
    die("lseek");
  if(write(fsfd, buf, BSIZE) != BSIZE)
    die("write");
}

// 将 inode ip 写入磁盘
void
winode(uint inum, struct dinode *ip)
{
  char buf[BSIZE];
  uint bn;
  struct dinode *dip;

  bn = IBLOCK(inum, sb); // 获取 inode 所在的块号
  rsect(bn, buf); // 读取该块
  dip = ((struct dinode*)buf) + (inum % IPB); // 定位到该 inode
  *dip = *ip; // 复制 inode 内容
  wsect(bn, buf); // 写回该块
}

// 从磁盘读取 inode inum
void
rinode(uint inum, struct dinode *ip)
{
  char buf[BSIZE];
  uint bn;
  struct dinode *dip;

  bn = IBLOCK(inum, sb); // 获取 inode 所在的块号
  rsect(bn, buf); // 读取该块
  dip = ((struct dinode*)buf) + (inum % IPB); // 定位到该 inode
  *ip = *dip; // 复制 inode 内容
}

// 从扇区 sec 中读取内容到 buf
void
rsect(uint sec, void *buf)
{
  if(lseek(fsfd, sec * BSIZE, 0) != sec * BSIZE)
    die("lseek");
  if(read(fsfd, buf, BSIZE) != BSIZE)
    die("read");
}

// 分配一个指定类型的 inode
uint
ialloc(ushort type)
{
  uint inum = freeinode++;
  struct dinode din;

  bzero(&din, sizeof(din)); // 清零 inode
  din.type = xshort(type); // 设置类型
  din.nlink = xshort(1); // 设置链接数
  din.size = xint(0); // 设置大小
  winode(inum, &din); // 将 inode 写入磁盘
  return inum;
}

// 更新位图，标记已使用的块
void
balloc(int used)
{
  uchar buf[BSIZE];
  int i;

  printf("balloc: 前 %d 个块已被分配\n", used);
  assert(used < BPB);
  bzero(buf, BSIZE);
  // 遍历所有已使用的块，在位图中设置相应的位
  for(i = 0; i < used; i++){
    buf[i/8] = buf[i/8] | (0x1 << (i%8));
  }
  printf("balloc: 在扇区 %d 写入位图块\n", sb.bmapstart);
  wsect(sb.bmapstart, buf);
}

#define min(a, b) ((a) < (b) ? (a) : (b))

// 将 xp 指向的数据追加到 inode inum
void
iappend(uint inum, void *xp, int n)
{
  char *p = (char*)xp;
  uint fbn, off, n1;
  struct dinode din;
  char buf[BSIZE];
  uint indirect[NINDIRECT];
  uint x;

  rinode(inum, &din); // 读取 inode
  off = xint(din.size); // 获取当前文件大小
  // printf("append inum %d at off %d sz %d\n", inum, off, n);
  while(n > 0){
    fbn = off / BSIZE; // 计算文件块号
    assert(fbn < MAXFILE);
    if(fbn < NDIRECT){ // 直接块
      if(xint(din.addrs[fbn]) == 0){
        din.addrs[fbn] = xint(freeblock++); // 分配新块
      }
      x = xint(din.addrs[fbn]);
    } else { // 间接块
      if(xint(din.addrs[NDIRECT]) == 0){
        din.addrs[NDIRECT] = xint(freeblock++); // 分配间接块
      }
      rsect(xint(din.addrs[NDIRECT]), (char*)indirect); // 读取间接块
      if(indirect[fbn - NDIRECT] == 0){
        indirect[fbn - NDIRECT] = xint(freeblock++); // 分配新块
        wsect(xint(din.addrs[NDIRECT]), (char*)indirect); // 写回间接块
      }
      x = xint(indirect[fbn-NDIRECT]);
    }
    n1 = min(n, (fbn + 1) * BSIZE - off); // 计算本次可以写入的字节数
    rsect(x, buf); // 读取数据块
    bcopy(p, buf + off - (fbn * BSIZE), n1); // 复制数据
    wsect(x, buf); // 写回数据块
    n -= n1;
    off += n1;
    p += n1;
  }
  din.size = xint(off); // 更新文件大小
  winode(inum, &din); // 写回 inode
}

// 打印错误信息并退出
void
die(const char *s)
{
  perror(s);
  exit(1);
}
