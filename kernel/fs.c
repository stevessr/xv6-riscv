// 文件系统实现。分为五层：
//  + 块 (Blocks): 原始磁盘块的分配器。
//  + 日志 (Log): 用于多步更新的崩溃恢复。
//  + 文件 (Files): inode 分配器，读、写、元数据。
//  + 目录 (Directories): 具有特殊内容的 inode（其他 inode 的列表！）
//  + 名称 (Names): 像 /usr/rtm/xv6/fs.c 这样的路径，用于方便地命名。
//
// 此文件包含底层文件系统操作例程。
// （更高级别的）系统调用实现在 sysfile.c 中。

#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "param.h"
#include "stat.h"
#include "spinlock.h"
#include "proc.h"
#include "sleeplock.h"
#include "fs.h"
#include "buf.h"
#include "file.h"

#define min(a, b) ((a) < (b) ? (a) : (b))
// 每个磁盘设备应该有一个超级块，但我们只使用一个设备
struct superblock sb; 

// 读取超级块。
static void
readsb(int dev, struct superblock *sb)
{
  struct buf *bp;

  bp = bread(dev, 1); // 超级块总是在块 1
  memmove(sb, bp->data, sizeof(*sb));
  brelse(bp);
}

// 初始化文件系统
void
fsinit(int dev) {
  readsb(dev, &sb);
  if(sb.magic != FSMAGIC) // 检查文件系统幻数
    panic("invalid file system");
  initlog(dev, &sb); // 初始化日志系统
}

// 将一个块清零。
static void
bzero(int dev, int bno)
{
  struct buf *bp;

  bp = bread(dev, bno);
  memset(bp->data, 0, BSIZE);
  log_write(bp); // 写入日志
  brelse(bp);
}

// 块管理。

// 分配一个清零的磁盘块。
// 如果磁盘空间不足，则返回 0。
static uint
balloc(uint dev)
{
  int b, bi, m;
  struct buf *bp;

  bp = 0;
  // 遍历位图块
  for(b = 0; b < sb.size; b += BPB){
    bp = bread(dev, BBLOCK(b, sb));
    for(bi = 0; bi < BPB && b + bi < sb.size; bi++){
      m = 1 << (bi % 8);
      if((bp->data[bi/8] & m) == 0){  // 块是空闲的吗？
        bp->data[bi/8] |= m;  // 标记块为已使用。
        log_write(bp);
        brelse(bp);
        bzero(dev, b + bi); // 将该块清零
        return b + bi;
      }
    }
    brelse(bp);
  }
  printf("balloc: 无块可用\n");
  return 0;
}

// 释放一个磁盘块。
static void
bfree(int dev, uint b)
{
  struct buf *bp;
  int bi, m;

  bp = bread(dev, BBLOCK(b, sb));
  bi = b % BPB;
  m = 1 << (bi % 8);
  if((bp->data[bi/8] & m) == 0)
    panic("释放 自由 块");
  bp->data[bi/8] &= ~m; // 清除位图中的相应位
  log_write(bp);
  brelse(bp);
}

// Inode。
//
// inode 描述了一个未命名的文件。
// 磁盘上的 inode 结构保存了元数据：文件类型、大小、链接数以及
// 保存文件内容的块列表。
//
// inode 在磁盘上从块 sb.inodestart 开始顺序排列。
// 每个 inode 都有一个编号，表示其在磁盘上的位置。
//
// 内核在内存中维护一个正在使用的 inode 表，
// 为多个进程访问 inode 提供同步点。
// 内存中的 inode 包含一些不存储在磁盘上的簿记信息：ip->ref 和 ip->valid。
//
// 一个 inode 及其内存表示在使用之前会经历一系列状态。
//
// * 分配：如果一个 inode 的类型（在磁盘上）非零，则它被分配了。
//   ialloc() 分配 inode，如果引用计数和链接计数都降为零，iput() 会释放它。
//
// * 表中引用：如果 ip->ref 为零，则 inode 表中的条目是空闲的。
//   否则，ip->ref 跟踪指向该条目的内存指针数量（打开的文件和当前目录）。
//   iget() 查找或创建一个表条目并增加其引用计数；iput() 减少引用计数。
//
// * 有效：只有当 ip->valid 为 1 时，inode 表条目中的信息（类型、大小等）才是正确的。
//   ilock() 从磁盘读取 inode 并设置 ip->valid，而如果 ip->ref 已降为零，iput() 会清除 ip->valid。
//
// * 锁定：文件系统代码只有在首先锁定了 inode 之后，才能检查和修改
//   inode 及其内容中的信息。
//
// 因此，一个典型的序列是：
//   ip = iget(dev, inum)
//   ilock(ip)
//   ... 检查和修改 ip->xxx ...
//   iunlock(ip)
//   iput(ip)
//
// ilock() 与 iget() 是分开的，这样系统调用可以
// 获得对 inode 的长期引用（如打开的文件），
// 并且只在短时间内锁定它（例如，在 read() 中）。
// 这种分离还有助于避免在路径名查找期间出现死锁和竞争。
// iget() 增加 ip->ref，使 inode 留在表中，并且指向它的指针保持有效。
//
// 许多内部文件系统函数都希望调用者已经锁定了相关的 inode；
// 这使得调用者可以创建多步原子操作。
//
// itable.lock 自旋锁保护 itable 条目的分配。
// 由于 ip->ref 指示条目是否空闲，ip->dev 和 ip->inum 指示条目持有哪个 i-node，
// 因此在使用这些字段时必须持有 itable.lock。
//
// ip->lock 休眠锁保护除 ref、dev 和 inum 之外的所有 ip-> 字段。
// 为了读取或写入 inode 的 ip->valid、ip->size、ip->type 等，必须持有 ip->lock。

// inode 表
struct {
  struct spinlock lock;
  struct inode inode[NINODE];
} itable;

void
iinit()
{
  int i = 0;
  
  initlock(&itable.lock, "itable");
  for(i = 0; i < NINODE; i++) {
    initsleeplock(&itable.inode[i].lock, "inode");
  }
}

static struct inode* iget(uint dev, uint inum);

// 在设备 dev 上分配一个 inode。
// 通过赋予它类型 type 来标记它为已分配。
// 返回一个未锁定但已分配并被引用的 inode，
// 如果没有空闲 inode，则返回 NULL。
struct inode*
ialloc(uint dev, short type)
{
  int inum;
  struct buf *bp;
  struct dinode *dip;

  for(inum = 1; inum < sb.ninodes; inum++){
    bp = bread(dev, IBLOCK(inum, sb));
    dip = (struct dinode*)bp->data + inum%IPB;
    if(dip->type == 0){  // 一个空闲的 inode
      memset(dip, 0, sizeof(*dip));
      dip->type = type;
      log_write(bp);   // 在磁盘上标记为已分配
      brelse(bp);
      return iget(dev, inum);
    }
    brelse(bp);
  }
  printf("ialloc: no inodes\n");
  return 0;
}

// 将修改过的内存中的 inode 复制到磁盘。
// 每次更改磁盘上存在的 ip->xxx 字段后都必须调用此函数。
// 调用者必须持有 ip->lock。
void
iupdate(struct inode *ip)
{
  struct buf *bp;
  struct dinode *dip;

  bp = bread(ip->dev, IBLOCK(ip->inum, sb));
  dip = (struct dinode*)bp->data + ip->inum%IPB;
  dip->type = ip->type;
  dip->major = ip->major;
  dip->minor = ip->minor;
  dip->nlink = ip->nlink;
  dip->size = ip->size;
  memmove(dip->addrs, ip->addrs, sizeof(ip->addrs));
  log_write(bp);
  brelse(bp);
}

// 在设备 dev 上查找编号为 inum 的 inode
// 并返回其内存中的副本。不锁定
// inode，也不从磁盘读取它。
static struct inode*
iget(uint dev, uint inum)
{
  struct inode *ip, *empty;

  acquire(&itable.lock);

  // inode 是否已经在表中？
  empty = 0;
  for(ip = &itable.inode[0]; ip < &itable.inode[NINODE]; ip++){
    if(ip->ref > 0 && ip->dev == dev && ip->inum == inum){
      ip->ref++;
      release(&itable.lock);
      return ip;
    }
    if(empty == 0 && ip->ref == 0)    // 记住空槽位。
      empty = ip;
  }

  // 回收一个 inode 条目。
  if(empty == 0)
    panic("iget: no inodes");

  ip = empty;
  ip->dev = dev;
  ip->inum = inum;
  ip->ref = 1;
  ip->valid = 0;
  release(&itable.lock);

  return ip;
}

// 增加 ip 的引用计数。
// 返回 ip 以支持 ip = idup(ip1) 的用法。
struct inode*
idup(struct inode *ip)
{
  acquire(&itable.lock);
  ip->ref++;
  release(&itable.lock);
  return ip;
}

// 锁定给定的 inode。
// 必要时从磁盘读取 inode。
void
ilock(struct inode *ip)
{
  struct buf *bp;
  struct dinode *dip;

  if(ip == 0 || ip->ref < 1)
    panic("ilock");

  acquiresleep(&ip->lock);

  if(ip->valid == 0){ // 如果内存中的 inode 无效
    bp = bread(ip->dev, IBLOCK(ip->inum, sb));
    dip = (struct dinode*)bp->data + ip->inum%IPB;
    ip->type = dip->type;
    ip->major = dip->major;
    ip->minor = dip->minor;
    ip->nlink = dip->nlink;
    ip->size = dip->size;
    memmove(ip->addrs, dip->addrs, sizeof(ip->addrs));
    brelse(bp);
    ip->valid = 1; // 标记为有效
    if(ip->type == 0)
      panic("ilock: no type");
  }
}

// 解锁给定的 inode。
void
iunlock(struct inode *ip)
{
  if(ip == 0 || !holdingsleep(&ip->lock) || ip->ref < 1)
    panic("iunlock");

  releasesleep(&ip->lock);
}

// 减少对内存中 inode 的引用。
// 如果这是最后一个引用，inode 表条目可以被回收。
// 如果这是最后一个引用并且 inode 没有链接到它，
// 则释放磁盘上的 inode（及其内容）。
// 所有对 iput() 的调用都必须在事务内，
// 以防需要释放 inode。
void
iput(struct inode *ip)
{
  acquire(&itable.lock);

  if(ip->ref == 1 && ip->valid && ip->nlink == 0){
    // inode 没有链接也没有其他引用：截断并释放。

    // ip->ref == 1 意味着没有其他进程可以锁定 ip，
    // 所以这个 acquiresleep() 不会阻塞（或死锁）。
    acquiresleep(&ip->lock);

    release(&itable.lock);

    itrunc(ip); // 截断文件，释放其所有数据块
    ip->type = 0; // 标记为空闲
    iupdate(ip);  // 更新到磁盘
    ip->valid = 0;

    releasesleep(&ip->lock);

    acquire(&itable.lock);
  }

  ip->ref--;
  release(&itable.lock);
}

// 常见用法：先解锁，然后释放。
void
iunlockput(struct inode *ip)
{
  iunlock(ip);
  iput(ip);
}

// Inode 内容
//
// 与每个 inode 关联的内容（数据）存储在磁盘的块中。
// 前 NDIRECT 个块号列在 ip->addrs[] 中。
// 接下来的 NINDIRECT 个块列在块 ip->addrs[NDIRECT] 中。

// 返回 inode ip 中第 n 个块的磁盘块地址。
// 如果没有这样的块，bmap 会分配一个。
// 如果磁盘空间不足，则返回 0。
static uint
bmap(struct inode *ip, uint bn)
{
  uint addr, *a;
  struct buf *bp;

  if(bn < NDIRECT){ // 直接块
    if((addr = ip->addrs[bn]) == 0){ // 如果块未分配
      addr = balloc(ip->dev); // 分配新块
      if(addr == 0)
        return 0;
      ip->addrs[bn] = addr;
    }
    return addr;
  }
  bn -= NDIRECT;

  if(bn < NINDIRECT){ // 间接块
    // 加载间接块，必要时分配。
    if((addr = ip->addrs[NDIRECT]) == 0){
      addr = balloc(ip->dev);
      if(addr == 0)
        return 0;
      ip->addrs[NDIRECT] = addr;
    }
    bp = bread(ip->dev, addr);
    a = (uint*)bp->data;
    if((addr = a[bn]) == 0){
      addr = balloc(ip->dev);
      if(addr){
        a[bn] = addr;
        log_write(bp);
      }
    }
    brelse(bp);
    return addr;
  }

  panic("bmap: out of range"); // 超出范围
}

// 截断 inode（丢弃内容）。
// 调用者必须持有 ip->lock。
void
itrunc(struct inode *ip)
{
  int i, j;
  struct buf *bp;
  uint *a;

  // 释放直接块
  for(i = 0; i < NDIRECT; i++){
    if(ip->addrs[i]){
      bfree(ip->dev, ip->addrs[i]);
      ip->addrs[i] = 0;
    }
  }

  // 释放间接块
  if(ip->addrs[NDIRECT]){
    bp = bread(ip->dev, ip->addrs[NDIRECT]);
    a = (uint*)bp->data;
    for(j = 0; j < NINDIRECT; j++){
      if(a[j])
        bfree(ip->dev, a[j]);
    }
    brelse(bp);
    bfree(ip->dev, ip->addrs[NDIRECT]);
    ip->addrs[NDIRECT] = 0;
  }

  ip->size = 0;
  iupdate(ip);
}

// 从 inode 复制状态信息。
// 调用者必须持有 ip->lock。
void
stati(struct inode *ip, struct stat *st)
{
  st->dev = ip->dev;
  st->ino = ip->inum;
  st->type = ip->type;
  st->nlink = ip->nlink;
  st->size = ip->size;
}

// 从 inode 读取数据。
// 调用者必须持有 ip->lock。
// 如果 user_dst==1，则 dst 是用户虚拟地址；
// 否则，dst 是内核地址。
int
readi(struct inode *ip, int user_dst, uint64 dst, uint off, uint n)
{
  uint tot, m;
  struct buf *bp;

  if(off > ip->size || off + n < off) // 偏移量超出范围
    return 0;
  if(off + n > ip->size) // 读取字节数超过文件大小
    n = ip->size - off;

  for(tot=0; tot<n; tot+=m, off+=m, dst+=m){
    uint addr = bmap(ip, off/BSIZE);
    if(addr == 0)
      break;
    bp = bread(ip->dev, addr);
    m = min(n - tot, BSIZE - off%BSIZE);
    if(either_copyout(user_dst, dst, bp->data + (off % BSIZE), m) == -1) {
      brelse(bp);
      tot = -1;
      break;
    }
    brelse(bp);
  }
  return tot;
}

// 向 inode 写入数据。
// 调用者必须持有 ip->lock。
// 如果 user_src==1，则 src 是用户虚拟地址；
// 否则，src 是内核地址。
// 返回成功写入的字节数。
// 如果返回值小于请求的 n，则表示发生了某种错误。
int
writei(struct inode *ip, int user_src, uint64 src, uint off, uint n)
{
  uint tot, m;
  struct buf *bp;

  if(off > ip->size || off + n < off)
    return -1;
  if(off + n > MAXFILE*BSIZE) // 文件大小超出限制
    return -1;

  for(tot=0; tot<n; tot+=m, off+=m, src+=m){
    uint addr = bmap(ip, off/BSIZE);
    if(addr == 0)
      break;
    bp = bread(ip->dev, addr);
    m = min(n - tot, BSIZE - off%BSIZE);
    if(either_copyin(bp->data + (off % BSIZE), user_src, src, m) == -1) {
      brelse(bp);
      break;
    }
    log_write(bp);
    brelse(bp);
  }

  if(off > ip->size)
    ip->size = off; // 更新文件大小

  // 即使大小没有改变，也要将 i-node 写回磁盘，
  // 因为上面的循环可能调用了 bmap() 并向 ip->addrs[] 添加了一个新块。
  iupdate(ip);

  return tot;
}

// 目录

int
namecmp(const char *s, const char *t)
{
  return strncmp(s, t, DIRSIZ);
}

// 在目录中查找目录项。
// 如果找到，将 *poff 设置为条目的字节偏移量。
struct inode*
dirlookup(struct inode *dp, char *name, uint *poff)
{
  uint off, inum;
  struct dirent de;

  if(dp->type != T_DIR)
    panic("dirlookup not DIR");

  for(off = 0; off < dp->size; off += sizeof(de)){
    if(readi(dp, 0, (uint64)&de, off, sizeof(de)) != sizeof(de))
      panic("dirlookup read");
    if(de.inum == 0) // 空的目录项
      continue;
    if(namecmp(name, de.name) == 0){
      // 条目与路径元素匹配
      if(poff)
        *poff = off;
      inum = de.inum;
      return iget(dp->dev, inum);
    }
  }

  return 0;
}

// 将一个新的目录项 (name, inum) 写入目录 dp。
// 成功返回 0，失败返回 -1（例如磁盘块不足）。
int
dirlink(struct inode *dp, char *name, uint inum)
{
  int off;
  struct dirent de;
  struct inode *ip;

  // 检查名称是否已存在。
  if((ip = dirlookup(dp, name, 0)) != 0){
    iput(ip);
    return -1;
  }

  // 查找一个空的目录项。
  for(off = 0; off < dp->size; off += sizeof(de)){
    if(readi(dp, 0, (uint64)&de, off, sizeof(de)) != sizeof(de))
      panic("dirlink read");
    if(de.inum == 0)
      break;
  }

  strncpy(de.name, name, DIRSIZ);
  de.inum = inum;
  if(writei(dp, 0, (uint64)&de, off, sizeof(de)) != sizeof(de))
    return -1;

  return 0;
}

// 路径

// 从 path 中复制下一个路径元素到 name。
// 返回一个指向复制元素之后的指针。
// 返回的路径没有前导斜杠，
// 所以调用者可以检查 *path=='\0' 来判断 name 是否是最后一个。
// 如果没有名称要删除，则返回 0。
//
// 示例:
//   skipelem("a/bb/c", name) = "bb/c", 设置 name = "a"
//   skipelem("///a//bb", name) = "bb", 设置 name = "a"
//   skipelem("a", name) = "", 设置 name = "a"
//   skipelem("", name) = skipelem("////", name) = 0
//
static char*
skipelem(char *path, char *name)
{
  char *s;
  int len;

  while(*path == '/') // 跳过前导斜杠
    path++;
  if(*path == 0)
    return 0;
  s = path;
  while(*path != '/' && *path != 0)
    path++;
  len = path - s;
  if(len >= DIRSIZ)
    memmove(name, s, DIRSIZ);
  else {
    memmove(name, s, len);
    name[len] = 0;
  }
  while(*path == '/') // 跳过多余的斜杠
    path++;
  return path;
}

// 查找并返回路径名的 inode。
// 如果 parent != 0，则返回父目录的 inode 并将最终
// 路径元素复制到 name 中，name 必须有 DIRSIZ 字节的空间。
// 必须在事务内调用，因为它调用了 iput()。
static struct inode*
namex(char *path, int nameiparent, char *name)
{
  struct inode *ip, *next;

  if(*path == '/') // 绝对路径
    ip = iget(ROOTDEV, ROOTINO);
  else // 相对路径
    ip = idup(myproc()->cwd);

  while((path = skipelem(path, name)) != 0){
    ilock(ip);
    if(ip->type != T_DIR){ // 当前路径组件不是目录
      iunlockput(ip);
      return 0;
    }
    if(nameiparent && *path == '\0'){
      // 提前一层停止，返回父目录
      iunlock(ip);
      return ip;
    }
    if((next = dirlookup(ip, name, 0)) == 0){ // 在当前目录中查找
      iunlockput(ip);
      return 0;
    }
    iunlockput(ip);
    ip = next; // 进入下一级目录
  }
  if(nameiparent){ // 如果是查找父目录，但路径已经解析完毕
    iput(ip);
    return 0;
  }
  return ip;
}

// 查找并返回路径的 inode
struct inode*
namei(char *path)
{
  char name[DIRSIZ];
  return namex(path, 0, name);
}

// 查找并返回路径的父目录 inode
struct inode*
nameiparent(char *path, char *name)
{
  return namex(path, 1, name);
}
