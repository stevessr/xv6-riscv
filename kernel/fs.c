// 文件系统实现。
//
// xv6 的文件系统被组织成一个分层结构，方便管理和扩展。
// 从底层到高层，依次是：
//   + 块 (Blocks):       磁盘块分配器，负责管理物理磁盘块的分配和回收。
//   + 日志 (Log):         提供崩溃恢复功能。对于需要多个写操作的更新，日志系统可以保证这些操作的原子性，
//                       防止系统在更新过程中崩溃导致文件系统不一致。
//   + 文件 (Files):       管理 inode，包括分配、读写数据和元数据。每个文件在文件系统中都由一个 inode 表示。
//   + 目录 (Directories): 一种特殊的 inode，其内容是目录项（dirent）的列表，每个目录项包含一个文件名和对应的 inode 编号。
//   + 路径名 (Names):     用户友好的文件路径，如 "/usr/rtm/xv6/fs.c"。路径名解析层将路径转换为对应的 inode。
//
// 此文件实现了底层的块、日志、文件和目录操作。
// 更高层次的、与文件描述符相关的系统调用（如 open, read, write）在 sysfile.c 中实现。

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
// 全局超级块。理论上每个磁盘设备都应有一个超级块，
// 但在 xv6 中我们只使用一个设备，所以定义一个全局变量即可。
struct superblock sb; 

// 从指定的设备读取超级块。
// dev: 设备号
// sb:  一个指向用于存放超级块数据的结构体的指针
static void
readsb(int dev, struct superblock *sb)
{
  struct buf *bp;

  // 超级块位于磁盘的第 1 号块 (磁盘块从0开始编号，0号是引导区)。
  bp = bread(dev, 1);
  // 将磁盘块中的数据拷贝到内存中的超级块结构体。
  memmove(sb, bp->data, sizeof(*sb));
  // 释放缓冲区。
  brelse(bp);
}

// 初始化文件系统。
// dev: 设备号
void
fsinit(int dev) {
  // 读取超级块信息。
  readsb(dev, &sb);
  // 检查文件系统幻数是否正确，以确认这是一个合法的 xv6 文件系统。
  if(sb.magic != FSMAGIC)
    panic("invalid file system");
  // 初始化日志系统，日志区的位置和大小由超级块指定。
  initlog(dev, &sb);
}

// 将一个磁盘块的内容清零。
// dev: 设备号
// bno: 块号
static void
bzero(int dev, int bno)
{
  struct buf *bp;

  // 读取指定的块到缓冲区。
  bp = bread(dev, bno);
  // 将缓冲区的数据全部设置为 0。
  memset(bp->data, 0, BSIZE);
  // 将写操作记录到日志中，确保即使发生崩溃，清零操作也能完成。
  log_write(bp);
  // 释放缓冲区。
  brelse(bp);
}

//
// 块管理 (Block management)
//

// 分配一个空闲的、内容已清零的磁盘块。
// dev: 设备号
// 成功时返回新分配的块号，如果磁盘空间耗尽则返回 0。
static uint
balloc(uint dev)
{
  int b, bi, m;
  struct buf *bp;

  bp = 0;
  // 遍历磁盘的所有块，以查找位图中的一个空闲位。
  // sb.size 是磁盘总块数。
  // BBLOCK 宏计算给定块号 b 所在的位图块的块号。
  for(b = 0; b < sb.size; b += BPB){ // BPB 是每个位图块能表示的块数
    bp = bread(dev, BBLOCK(b, sb));
    for(bi = 0; bi < BPB && b + bi < sb.size; bi++){
      m = 1 << (bi % 8); // 计算当前位在字节中的掩码
      if((bp->data[bi/8] & m) == 0){  // 检查该位是否为0，0表示空闲
        bp->data[bi/8] |= m;  // 将该位置1，标记为已使用。
        log_write(bp);      // 记录对位图块的修改。
        brelse(bp);
        bzero(dev, b + bi); // 将新分配的块清零。
        return b + bi;      // 返回新分配的块号。
      }
    }
    brelse(bp);
  }
  printf("balloc: 无块可用\n");
  return 0;
}

// 释放一个指定的磁盘块。
// dev: 设备号
// b:   要释放的块号
static void
bfree(int dev, uint b)
{
  struct buf *bp;
  int bi, m;

  // 读取包含块 b 对应位图信息的块。
  bp = bread(dev, BBLOCK(b, sb));
  bi = b % BPB;
  m = 1 << (bi % 8);
  // 检查该块是否确实已被分配。
  if((bp->data[bi/8] & m) == 0)
    panic("bfree: 释放自用块");
  // 将位图中的对应位清零，标记为可用。
  bp->data[bi/8] &= ~m;
  // 将对位图的修改写入日志。
  log_write(bp);
  brelse(bp);
}

//
// Inodes
//
// inode 描述了一个文件或目录，但不包含其名字。
// 磁盘上的 inode 结构 (struct dinode) 存储了文件的元数据：
// 类型 (文件、目录、设备)、大小、链接数以及指向文件数据块的指针数组。
//
// 所有的 inode 都存储在磁盘上从 sb.inodestart 开始的一片连续区域中。
// 每个 inode 都有一个唯一的编号 (inum)，这个编号就是它在 inode 区的索引。
//
// 内核在内存中维护一个 inode 缓存 (itable)，用于缓存活跃的 inode。
// 这个缓存层为多个进程并发访问 inode 提供了同步点。
// 内存中的 inode (struct inode) 包含了一些磁盘上没有的簿记信息，
// 主要是 ip->ref (引用计数) 和 ip->valid (有效位)。
//
// 一个 inode 实例从创建到销毁会经历以下几种状态：
//
// * 已分配 (Allocated): 磁盘上的 inode 类型 (type) 非零。ialloc() 分配一个 inode，
//   当 iput() 发现引用计数和链接计数都为零时，会释放它。
//
// * 在缓存中被引用 (Referenced in table): 如果 ip->ref 为零，表示 itable 中的这个槽位是空闲的。
//   否则，ip->ref 记录了有多少个内存指针指向这个 inode (例如，打开的文件、当前工作目录等)。
//   iget() 查找或创建一个 itable 条目并增加引用计数；iput() 减少引用计数。
//
// * 有效 (Valid): 当 ip->valid 为 1 时，表示内存 inode 中的数据（类型、大小等）是从磁盘同步过来的，是有效的。
//   ilock() 会在需要时从磁盘读取 inode 数据并设置 ip->valid。
//   如果 ip->ref 降为零，iput() 会清除 ip->valid。
//
// * 锁定 (Locked): 文件系统代码在访问或修改 inode 的元数据或内容之前，必须先锁定该 inode。
//   锁定可以防止并发访问导致的数据竞争和不一致。
//
// 因此，一个典型的 inode 操作序列如下：
//   ip = iget(dev, inum); // 获取 inode 的内存引用
//   ilock(ip);            // 锁定 inode，并从磁盘读取数据（如果需要）
//   ... 检查和修改 ip->xxx ...
//   iupdate(ip);          // 将修改写回磁盘
//   iunlock(ip);          // 解锁 inode
//   iput(ip);             // 释放对 inode 的内存引用
//
// ilock() 和 iget() 是分开的，这使得系统调用可以长时间持有一个 inode 的引用
// (比如一个打开的文件)，但只在需要读写时才短暂地锁定它。
// 这种设计有助于避免在路径名查找等复杂操作中发生死锁。
// iget() 增加 ip->ref，确保 inode 留在缓存中，指针持续有效。
//
// 许多文件系统内部函数都假定调用者已经持有了相关 inode 的锁，
// 这使得调用者可以将多个操作组合成一个原子操作。
//
// itable.lock (自旋锁) 保护 itable 结构本身，特别是 inode 的分配和查找过程。
// 对 ip->ref, ip->dev, ip->inum 的访问必须在持有 itable.lock 的情况下进行。
//
// ip->lock (休眠锁) 保护 inode 的所有其他字段（如 type, size, addrs 等）以及文件内容。
// 读写这些字段前必须持有 ip->lock。

// 内存中的 inode 缓存区
struct {
  struct spinlock lock;       // 保护 itable 的分配和查找
  struct inode inode[NINODE]; // inode 缓存数组
} itable;

// 初始化 inode 缓存 (itable)。
void
iinit()
{
  int i = 0;
  
  initlock(&itable.lock, "itable"); // 初始化 itable 的自旋锁
  for(i = 0; i < NINODE; i++) {
    initsleeplock(&itable.inode[i].lock, "inode"); // 初始化每个 inode 的休眠锁
  }
}

static struct inode* iget(uint dev, uint inum);

// 在设备 dev 上分配一个新的 inode。
// 分配时会将其类型设置为 type。
// 返回一个未锁定但已被引用 (ref=1) 的 inode。
// 如果磁盘上没有空闲的 inode，则返回 NULL (0)。
struct inode*
ialloc(uint dev, short type)
{
  int inum;
  struct buf *bp;
  struct dinode *dip;

  // 遍历磁盘上的所有 inode，查找一个空闲的（类型为0）。
  for(inum = 1; inum < sb.ninodes; inum++){
    bp = bread(dev, IBLOCK(inum, sb)); // IBLOCK 宏计算 inum 所在的块号
    dip = (struct dinode*)bp->data + inum%IPB; // IPB 是每块包含的 inode 数
    if(dip->type == 0){  // 找到了一个空闲 inode
      memset(dip, 0, sizeof(*dip)); // 清零
      dip->type = type; // 设置类型
      log_write(bp);   // 将修改写入日志
      brelse(bp);
      return iget(dev, inum); // 获取该 inode 的内存缓存实例
    }
    brelse(bp);
  }
  printf("ialloc: 无可用inodes\n");
  return 0;
}

// 将内存中已修改的 inode 写回磁盘。
// 任何对 inode 磁盘字段 (ip->xxx) 的修改之后都必须调用此函数。
// 调用者必须持有 ip->lock。
void
iupdate(struct inode *ip)
{
  struct buf *bp;
  struct dinode *dip;

  bp = bread(ip->dev, IBLOCK(ip->inum, sb));
  dip = (struct dinode*)bp->data + ip->inum%IPB;
  // 从内存 inode 拷贝元数据到磁盘 inode 结构。
  dip->type = ip->type;
  dip->major = ip->major;
  dip->minor = ip->minor;
  dip->nlink = ip->nlink;
  dip->size = ip->size;
  memmove(dip->addrs, ip->addrs, sizeof(ip->addrs));
  log_write(bp); // 记录写操作
  brelse(bp);
}

// 在 inode 缓存 (itable) 中查找指定设备和 inode 编号的 inode。
// 如果找到，增加其引用计数并返回。
// 如果没找到，会从 itable 中找一个空闲槽位分配给它，并返回。
// 这个函数不锁定 inode，也不从磁盘读取数据。
static struct inode*
iget(uint dev, uint inum)
{
  struct inode *ip, *empty;

  acquire(&itable.lock); // 获取 itable 锁以保证操作的原子性

  // 遍历 itable，查找 inode 是否已在缓存中。
  empty = 0;
  for(ip = &itable.inode[0]; ip < &itable.inode[NINODE]; ip++){
    if(ip->ref > 0 && ip->dev == dev && ip->inum == inum){
      ip->ref++; // 找到了，增加引用计数
      release(&itable.lock);
      return ip;
    }
    if(empty == 0 && ip->ref == 0)    // 记录下第一个空闲的槽位
      empty = ip;
  }

  // 缓存中没有，需要分配一个新的 itable 条目。
  if(empty == 0)
    panic("iget: no inodes");

  // 使用找到的空闲槽位。
  ip = empty;
  ip->dev = dev;
  ip->inum = inum;
  ip->ref = 1;
  ip->valid = 0; // 标记为无效，因为数据还未从磁盘读取
  release(&itable.lock);

  return ip;
}

// 增加 inode ip 的引用计数。
// 返回 ip 本身，方便链式调用，如 `ip = idup(ip1);`
struct inode*
idup(struct inode *ip)
{
  acquire(&itable.lock);
  ip->ref++;
  release(&itable.lock);
  return ip;
}

// 锁定给定的 inode。
// 在返回前，会确保 inode 的内容在内存中是有效的（即 ip->valid == 1）。
// 如果无效，会从磁盘读取 inode 数据。
void
ilock(struct inode *ip)
{
  struct buf *bp;
  struct dinode *dip;

  if(ip == 0 || ip->ref < 1)
    panic("ilock");

  acquiresleep(&ip->lock); // 获取该 inode 的休眠锁

  if(ip->valid == 0){ // 如果内存副本无效
    bp = bread(ip->dev, IBLOCK(ip->inum, sb));
    dip = (struct dinode*)bp->data + ip->inum%IPB;
    // 从磁盘 dinode 复制元数据到内存 inode。
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

// 减少对内存中 inode 的引用计数。
// 如果这是最后一个引用 (ip->ref == 1)，并且 inode 没有链接 (ip->nlink == 0)，
// 则会释放磁盘上的 inode 及其所有数据块。
// 如果这是最后一个引用，但仍有链接，inode 缓存条目可以被回收，但磁盘数据保留。
// 所有对 iput 的调用都应在一个事务中，以防需要释放 inode。
void
iput(struct inode *ip)
{
  acquire(&itable.lock);

  if(ip->ref == 1 && ip->valid && ip->nlink == 0){
    // inode 没有链接，也没有其他内存引用：可以安全释放。
    
    // 因为 ip->ref == 1，意味着没有其他进程持有这个 inode 的引用，
    // 所以这里的 acquiresleep 不会阻塞或死锁。
    acquiresleep(&ip->lock);

    release(&itable.lock);

    itrunc(ip); // 截断文件，释放所有数据块。
    ip->type = 0; // 标记为空闲。
    iupdate(ip);  // 将状态写回磁盘。
    ip->valid = 0; // 标记内存副本为无效。

    releasesleep(&ip->lock);

    acquire(&itable.lock);
  }

  ip->ref--;
  release(&itable.lock);
}

// 一个方便的组合操作：先解锁 inode，然后释放对它的引用。
void
iunlockput(struct inode *ip)
{
  iunlock(ip);
  iput(ip);
}

//
// Inode content
//
// 每个 inode 关联的数据内容存储在一系列磁盘块中。
// 前 NDIRECT 个数据块的地址直接存储在 ip->addrs[] 数组中。
// 之后的 NINDIRECT 个数据块的地址存储在一个间接块中，该间接块的地址是 ip->addrs[NDIRECT]。
//

// 返回 inode ip 的第 bn 个逻辑数据块对应的物理磁盘块号。
// 如果该逻辑块尚未分配，bmap 会为其分配一个新的物理块。
// 如果分配失败（如磁盘已满），则返回 0。
static uint
bmap(struct inode *ip, uint bn)
{
  uint addr, *a;
  struct buf *bp;

  if(bn < NDIRECT){ // 直接块
    if((addr = ip->addrs[bn]) == 0){ // 如果块未分配
      addr = balloc(ip->dev); // 分配新块
      if(addr == 0)
        return 0; // 分配失败
      ip->addrs[bn] = addr; // 记录新块地址
    }
    return addr;
  }
  bn -= NDIRECT; // 计算在间接块中的索引

  if(bn < NINDIRECT){ // 间接块
    // 加载间接块，如果间接块本身不存在，则先分配。
    if((addr = ip->addrs[NDIRECT]) == 0){
      addr = balloc(ip->dev);
      if(addr == 0)
        return 0;
      ip->addrs[NDIRECT] = addr;
    }
    bp = bread(ip->dev, addr); // 读取间接块
    a = (uint*)bp->data;
    if((addr = a[bn]) == 0){ // 如果间接块中的目标条目为空
      addr = balloc(ip->dev); // 分配新数据块
      if(addr){
        a[bn] = addr; // 在间接块中记录新数据块的地址
        log_write(bp); // 将对间接块的修改写入日志
      }
    }
    brelse(bp);
    return addr;
  }

  panic("bmap: out of range"); // 块号超出了单个间接块的范围
}

// 截断一个 inode (释放其所有数据块)。
// 调用者必须持有 ip->lock。
void
itrunc(struct inode *ip)
{
  int i, j;
  struct buf *bp;
  uint *a;

  // 释放所有直接块
  for(i = 0; i < NDIRECT; i++){
    if(ip->addrs[i]){
      bfree(ip->dev, ip->addrs[i]);
      ip->addrs[i] = 0;
    }
  }

  // 释放所有间接块指向的数据块
  if(ip->addrs[NDIRECT]){
    bp = bread(ip->dev, ip->addrs[NDIRECT]);
    a = (uint*)bp->data;
    for(j = 0; j < NINDIRECT; j++){
      if(a[j])
        bfree(ip->dev, a[j]);
    }
    brelse(bp);
    // 释放间接块本身
    bfree(ip->dev, ip->addrs[NDIRECT]);
    ip->addrs[NDIRECT] = 0;
  }

  ip->size = 0;
  iupdate(ip);
}

// 从 inode 复制状态信息到 stat 结构体。
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
// user_dst: 1 表示 dst 是用户空间地址，0 表示是内核地址。
// dst:      目标地址
// off:      读取的起始偏移量
// n:        要读取的字节数
// 返回成功读取的字节数，如果出错则返回 -1。
int
readi(struct inode *ip, int user_dst, uint64 dst, uint off, uint n)
{
  uint tot, m;
  struct buf *bp;

  if(off > ip->size || off + n < off)
    return 0; // 偏移量超出文件范围或溢出
  if(off + n > ip->size)
    n = ip->size - off; // 调整读取字节数，不超过文件末尾

  for(tot=0; tot<n; tot+=m, off+=m, dst+=m){
    uint addr = bmap(ip, off/BSIZE); // 获取逻辑块号对应的物理块号
    if(addr == 0)
      break; // 块未分配
    bp = bread(ip->dev, addr);
    m = min(n - tot, BSIZE - off%BSIZE); // 计算本次要读取的字节数
    // 从缓冲区复制数据到目标地址
    if(either_copyout(user_dst, dst, bp->data + (off % BSIZE), m) == -1) {
      brelse(bp);
      tot = -1; // 复制失败
      break;
    }
    brelse(bp);
  }
  return tot;
}

// 向 inode 写入数据。
// 调用者必须持有 ip->lock。
// user_src: 1 表示 src 是用户空间地址，0 表示是内核地址。
// src:      源地址
// off:      写入的起始偏移量
// n:        要写入的字节数
// 返回成功写入的字节数。如果返回值小于 n，表示发生了错误（如磁盘空间不足）。
int
writei(struct inode *ip, int user_src, uint64 src, uint off, uint n)
{
  uint tot, m;
  struct buf *bp;

  if(off > ip->size || off + n < off)
    return -1; // 偏移量超出文件范围或溢出
  if(off + n > MAXFILE*BSIZE) // 文件大小超出系统限制
    return -1;

  for(tot=0; tot<n; tot+=m, off+=m, src+=m){
    uint addr = bmap(ip, off/BSIZE); // 获取或分配物理块
    if(addr == 0)
      break; // 分配失败
    bp = bread(ip->dev, addr);
    m = min(n - tot, BSIZE - off%BSIZE); // 计算本次要写入的字节数
    // 从源地址复制数据到缓冲区
    if(either_copyin(bp->data + (off % BSIZE), user_src, src, m) == -1) {
      brelse(bp);
      break; // 复制失败
    }
    log_write(bp); // 记录写操作
    brelse(bp);
  }

  if(off > ip->size)
    ip->size = off; // 如果写入操作扩展了文件，则更新文件大小

  // 即使文件大小没有改变，也需要更新 inode 到磁盘，
  // 因为 bmap() 可能分配了新的数据块，修改了 ip->addrs[]。
  iupdate(ip);

  return tot;
}

//
// Directories
//

// 比较两个目录项名称是否相同。
int
namecmp(const char *s, const char *t)
{
  return strncmp(s, t, DIRSIZ);
}

// 在目录 dp 中查找名为 name 的目录项。
// 如果找到，返回对应的 inode，并通过 *poff 返回该目录项在目录文件中的偏移量。
// 如果找不到，返回 0。
// 调用者在使用返回的 inode 后必须调用 iput() 来释放它。
struct inode*
dirlookup(struct inode *dp, char *name, uint *poff)
{
  uint off, inum;
  struct dirent de;

  if(dp->type != T_DIR)
    panic("dirlookup not DIR");

  // 遍历目录文件中的所有目录项
  for(off = 0; off < dp->size; off += sizeof(de)){
    if(readi(dp, 0, (uint64)&de, off, sizeof(de)) != sizeof(de))
      panic("dirlookup read");
    if(de.inum == 0) // 跳过空的目录项
      continue;
    if(namecmp(name, de.name) == 0){
      // 找到了匹配的目录项
      if(poff)
        *poff = off; // 记录偏移量
      inum = de.inum;
      return iget(dp->dev, inum); // 返回 inode
    }
  }

  return 0; // 未找到
}

// 将一个新的目录项 (name, inum) 写入目录 dp。
// 成功返回 0，失败返回 -1（例如，名称已存在或目录已满）。
int
dirlink(struct inode *dp, char *name, uint inum)
{
  int off;
  struct dirent de;
  struct inode *ip;

  // 检查名称是否已存在。
  if((ip = dirlookup(dp, name, 0)) != 0){
    iput(ip); // 名称已存在，释放查找到的 inode
    return -1;
  }

  // 查找一个空的目录项 (de.inum == 0) 来写入新条目。
  for(off = 0; off < dp->size; off += sizeof(de)){
    if(readi(dp, 0, (uint64)&de, off, sizeof(de)) != sizeof(de))
      panic("dirlink read");
    if(de.inum == 0)
      break;
  }

  // 写入新的目录项
  strncpy(de.name, name, DIRSIZ);
  de.inum = inum;
  if(writei(dp, 0, (uint64)&de, off, sizeof(de)) != sizeof(de))
    return -1;

  return 0;
}

//
// Paths
//

// 从路径字符串 path 中提取出第一个路径元素到 name。
// 返回一个指向路径字符串中剩余部分的指针。
// 返回的路径指针不会有前导斜杠。
// 调用者可以检查 *path == '\0' 来判断 name 是否是最后一个元素。
// 如果没有名称可提取，则返回 0。
//
// 示例:
//   skipelem("a/bb/c", name) -> "bb/c", name = "a"
//   skipelem("///a//bb", name) -> "bb", name = "a"
//   skipelem("a", name) -> "", name = "a"
//   skipelem("", name) -> 0
//   skipelem("////", name) -> 0
//
static char*
skipelem(char *path, char *name)
{
  char *s;
  int len;

  while(*path == '/') // 跳过所有前导'/'
    path++;
  if(*path == 0) // 路径为空或只有'/'
    return 0;
  s = path;
  while(*path != '/' && *path != 0) // 找到路径元素的结尾
    path++;
  len = path - s;
  if(len >= DIRSIZ) // 路径元素名太长，截断
    memmove(name, s, DIRSIZ);
  else {
    memmove(name, s, len);
    name[len] = 0;
  }
  while(*path == '/') // 跳过元素后的所有'/'
    path++;
  return path;
}

// 逐级解析路径，返回最终的 inode。
// 如果 nameiparent 为 1，则查找并返回路径的父目录的 inode，
// 并将最后一个路径元素复制到 name 中。
// 这个函数必须在一个事务中被调用。
static struct inode*
namex(char *path, int nameiparent, char *name)
{
  struct inode *ip, *next;

  if(*path == '/') // 绝对路径，从根目录开始
    ip = iget(ROOTDEV, ROOTINO);
  else // 相对路径，从当前进程的工作目录开始
    ip = idup(myproc()->cwd);

  // 循环解析路径的每个部分
  while((path = skipelem(path, name)) != 0){
    ilock(ip);
    if(ip->type != T_DIR){ // 当前 inode 必须是目录才能继续解析
      iunlockput(ip);
      return 0;
    }
    if(nameiparent && *path == '\0'){
      // 如果是查找父目录，并且已经到达路径的最后一个元素，
      // 那么当前 ip 就是要找的父目录，直接返回。
      iunlock(ip);
      return ip;
    }
    // 在当前目录中查找下一个路径元素
    if((next = dirlookup(ip, name, 0)) == 0){
      iunlockput(ip);
      return 0; // 未找到
    }
    iunlockput(ip); // 释放当前目录 inode
    ip = next;      // 进入下一级目录
  }
  if(nameiparent){ // 如果是查找父目录，但路径本身就是'/'或'a/'，没有父目录。
    iput(ip);
    return 0;
  }
  return ip; // 返回最终找到的 inode
}

// 解析完整路径并返回其对应的 inode。
struct inode*
namei(char *path)
{
  char name[DIRSIZ];
  return namex(path, 0, name);
}

// 解析路径的父目录，并返回其 inode。
// 路径的最后一个文件名部分被复制到 name 中。
struct inode*
nameiparent(char *path, char *name)
{
  return namex(path, 1, name);
}
