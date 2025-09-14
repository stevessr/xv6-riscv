// 文件系统实现。分为五个层次：
//   + 块层(Blocks): 原始磁盘块的分配器。
//   + 日志层(Log): 为多步更新提供崩溃恢复功能。
//   + 文件层(Files): inode分配器，以及文件读、写、元数据管理。
//   + 目录层(Directories): 一种特殊的inode，其内容是其他inode的列表。
//   + 路径名层(Names): 支持像 /usr/rtm/xv6/fs.c 这样的方便的路径命名。
//
// 这个文件包含了底层的文件系统操作例程。
// 更高层次的系统调用实现位于 sysfile.c 文件中。


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

#define min(a, b) ((a) < (b) ? (a) : (b)) // 定义一个宏，返回两个数中的较小值
// 每个磁盘设备都应该有一个超级块，但我们当前只运行在一个设备上
struct superblock sb; // 全局的超级块（superblock）结构体变量

// 读取超级块
static void
readsb(int dev, struct superblock *sb)
{
  struct buf *bp;

  bp = bread(dev, 1); // 读取设备的第1块（超级块所在的位置）
  memmove(sb, bp->data, sizeof(*sb)); // 将读取的数据拷贝到sb结构体中
  brelse(bp); // 释放缓冲区
}

// 初始化文件系统
void
fsinit(int dev) {
  readsb(dev, &sb); // 读取超级块信息
  if(sb.magic != FSMAGIC) // 检查文件系统幻数是否正确
    panic("invalid file system"); // 如果不正确，说明文件系统无效，系统崩溃
  initlog(dev, &sb); // 初始化日志系统
  ireclaim(dev); // 回收孤立的inode
}

// 将一个块的内容清零
static void
bzero(int dev, int bno)
{
  struct buf *bp;

  bp = bread(dev, bno); // 读取指定的块
  memset(bp->data, 0, BSIZE); // 将块的数据区全部设置为0
  log_write(bp); // 将这个写操作记录到日志中（为了崩溃恢复）
  brelse(bp); // 释放缓冲区
}

// 块管理部分

// 分配一个内容清零的磁盘块
// 如果磁盘空间耗尽，则返回0
static uint
balloc(uint dev)
{
  int b, bi, m;
  struct buf *bp;

  bp = 0;
  // 遍历所有块，步长为每个位图块能表示的位数(BPB)
  for(b = 0; b < sb.size; b += BPB){
    bp = bread(dev, BBLOCK(b, sb)); // 读取管理块b的位图所在的块
    // 在这个位图块内，遍历每一位
    for(bi = 0; bi < BPB && b + bi < sb.size; bi++){
      m = 1 << (bi % 8); // 计算当前位(bi)在它所在字节中的掩码
      if((bp->data[bi/8] & m) == 0){  // Is block free? // 检查该位是否为0，如果是，则表示对应的块是空闲的
        bp->data[bi/8] |= m;  // Mark block in use. // 将该位置1，标记块为已使用
        log_write(bp); // 将对位图块的修改写入日志
        brelse(bp); // 释放位图块的缓冲区
        bzero(dev, b + bi); // 将新分配的数据块清零
        return b + bi; // 返回新分配的块号
      }
    }
    brelse(bp); // 释放位图块的缓冲区（如果在这个块里没找到空闲位）
  }
  printf("balloc: out of blocks\n"); // 如果遍历完所有位图块都没找到空闲块，则打印错误信息
  return 0; // 返回0表示分配失败
}

// Free a disk block.
// 释放一个磁盘块
static void
bfree(int dev, uint b)
{
  struct buf *bp;
  int bi, m;

  bp = bread(dev, BBLOCK(b, sb)); // 读取管理块b的位图所在的块
  bi = b % BPB; // 计算块b在位图块中的偏移（第几位）
  m = 1 << (bi % 8); // 计算这一位在它所在字节中的掩码
  if((bp->data[bi/8] & m) == 0) // 检查该位是否已经是0
    panic("freeing free block"); // 如果是0，表示正在释放一个本就是空闲的块，这是个错误
  bp->data[bi/8] &= ~m; // 将该位置0，标记块为空闲
  log_write(bp); // 将对位图块的修改写入日志
  brelse(bp); // 释放位图块的缓冲区
}

// Inode管理部分
//
// 磁盘上的inode结构体存储了元数据：文件类型、大小、被链接的次数，以及存放文件内容的块列表。
//
// inode在磁盘上从sb.inodestart块开始连续存放。每个inode都有一个编号，表示它在磁盘上的位置。
//
// 内核在内存中维护一个正在使用的inode表（缓存），为多个进程同步访问inode提供支持。
// 内存中的inode包含一些磁盘上没有的簿记信息：ip->ref（引用计数）和ip->valid（数据有效性标志）。
//
// An inode and its in-memory representation go through a
// sequence of states before they can be used by the
// rest of the file system code.
// 一个inode及其内存中的表示在使用前会经历一系列状态。
//
// * 分配状态：如果一个磁盘inode的类型不为0，则它是已分配的。ialloc()分配inode，当引用计数和链接数都降为0时，iput()释放它。
//
// * 缓存表中的引用状态：如果inode缓存表中一个条目的ip->ref为0，则该条目是空闲的。否则，ip->ref跟踪指向该条目的内存指针数量（如打开的文件和当前目录）。
//   iget()查找或创建一个缓存条目并增加其引用计数；iput()减少引用计数。
//
// * 有效状态：只有当ip->valid为1时，inode缓存条目中的信息（类型、大小等）才是正确的（即与磁盘同步）。
//   ilock()从磁盘读取inode信息并设置ip->valid，而当ip->ref降为0时，iput()会清除ip->valid。
//
// * 锁定状态：文件系统代码只有在首先锁定了inode之后，才能检查和修改inode中的信息及其内容。
//
// 因此，一个典型的操作序列是：
//   ip = iget(dev, inum)  // 获取inode的内存引用
//   ilock(ip)           // 锁定inode，并确保数据已从磁盘加载
//   ... 检查和修改ip->xxx ...
//   iunlock(ip)         // 解锁inode
//   iput(ip)            // 释放inode的内存引用
//
// ilock()与iget()是分开的，这样系统调用可以获得对inode的长期引用（例如打开的文件），
// 而只在短时间内锁定它（例如在read()中）。这种分离也有助于避免路径名查找期间的死锁和竞争条件。
// iget()增加ip->ref，使得inode保留在缓存表中，指向它的指针保持有效。
//
// 许多内部文件系统函数期望调用者已经锁定了相关的inode；这使得调用者可以创建多步的原子操作。
//
// itable.lock自旋锁保护itable条目的分配。因为ip->ref指示条目是否空闲，ip->dev和ip->inum指示条目对应哪个inode，
// 所以在使用这些字段时必须持有itable.lock。
//
// ip->lock睡眠锁保护除ref, dev, inum之外的所有ip->字段。
// 必须持有ip->lock才能读写该inode的ip->valid, ip->size, ip->type等字段。


struct {
  struct spinlock lock; // 保护整个inode缓存表的自旋锁
  struct inode inode[NINODE]; // inode缓存表数组
} itable;

void
iinit()
{
  int i = 0;
  
  initlock(&itable.lock, "itable"); // 初始化inode缓存表的锁
  for(i = 0; i < NINODE; i++) {
    initsleeplock(&itable.inode[i].lock, "inode"); // 初始化缓存表中每个inode条目的睡眠锁
  }
}

static struct inode* iget(uint dev, uint inum); // iget函数的前向声明

// 在设备dev上分配一个inode。
// 通过赋予它类型type来标记它为已分配。
// 返回一个未锁定但已分配且被引用的inode，如果没有空闲inode则返回NULL。
struct inode*
ialloc(uint dev, short type)
{
  int inum;
  struct buf *bp;
  struct dinode *dip;

  // 从1号inode开始遍历（0号通常是保留的）
  for(inum = 1; inum < sb.ninodes; inum++){
    bp = bread(dev, IBLOCK(inum, sb)); // 读取包含第inum个inode的磁盘块
    dip = (struct dinode*)bp->data + inum%IPB; // 计算该inode在块内的地址
    if(dip->type == 0){  // a free inode // 检查该磁盘inode的类型是否为0，0表示空闲
      memset(dip, 0, sizeof(*dip)); // 将这个dinode结构清零
      dip->type = type; // 设置新的类型，标记为已分配
      log_write(bp);   // mark it allocated on the disk // 将对磁盘块的修改写入日志
      brelse(bp); // 释放缓冲区
      return iget(dev, inum); // 获取这个新分配的inode的内存表示并返回
    }
    brelse(bp); // 如果此inode已被占用，释放缓冲区，继续寻找下一个
  }
  printf("ialloc: no inodes\n"); // 如果遍历完所有inode都没找到空闲的，则打印错误
  return 0; // 返回NULL表示分配失败
}

// Caller must hold ip->lock.
// 将修改过的内存inode拷贝回磁盘。
// 每次修改了ip->xxx中需要持久化到磁盘的字段后，都必须调用此函数。
// 调用者必须持有ip->lock。
void
iupdate(struct inode *ip)
{
  struct buf *bp;
  struct dinode *dip;

  bp = bread(ip->dev, IBLOCK(ip->inum, sb)); // 读取该inode所在的磁盘块
  dip = (struct dinode*)bp->data + ip->inum%IPB; // 定位到该inode在块中的具体位置
  dip->type = ip->type; // 更新类型
  dip->major = ip->major; // 更新主设备号
  dip->minor = ip->minor; // 更新次设备号
  dip->nlink = ip->nlink; // 更新链接数
  dip->size = ip->size; // 更新文件大小
  memmove(dip->addrs, ip->addrs, sizeof(ip->addrs)); // 更新数据块地址列表
  log_write(bp); // 将对磁盘块的修改写入日志
  brelse(bp); // 释放缓冲区
}

// 在设备dev上查找编号为inum的inode，并返回其在内存中的拷贝（缓存）。
// 此函数不锁定inode，也不从磁盘读取它。
static struct inode*
iget(uint dev, uint inum)
{
  struct inode *ip, *empty;

  acquire(&itable.lock); // 获取itable的锁，以保证对inode缓存表的原子操作

  // Is the inode already in the table?
  // 这个inode是否已经在缓存表中了？
  empty = 0; // 用于记录空闲槽位的指针
  for(ip = &itable.inode[0]; ip < &itable.inode[NINODE]; ip++){
    if(ip->ref > 0 && ip->dev == dev && ip->inum == inum){ // 如果引用计数大于0且设备号、inode号都匹配
      ip->ref++; // 增加引用计数
      release(&itable.lock); // 释放itable的锁
      return ip; // 返回找到的inode
    }
    if(empty == 0 && ip->ref == 0)    // Remember empty slot.
      empty = ip; // 如果找到一个引用计数为0的空闲槽位，记录下来
  }

  // Recycle an inode entry.
  // 如果没找到，就重用一个inode缓存条目。
  if(empty == 0) // 如果没有找到空闲槽位
    panic("iget: no inodes"); // 缓存表已满，系统崩溃

  ip = empty; // 使用之前找到的空闲槽位
  ip->dev = dev; // 设置设备号
  ip->inum = inum; // 设置inode号
  ip->ref = 1; // 设置引用计数为1
  ip->valid = 0; // 设置为无效，表示数据还未从磁盘加载
  release(&itable.lock); // 释放itable的锁

  return ip; // 返回新配置的内存inode
}

// 增加ip的引用计数。
// 返回ip是为了方便使用 ip = idup(ip1) 这样的写法。
struct inode*
idup(struct inode *ip)
{
  acquire(&itable.lock); // 获取itable的锁
  ip->ref++; // 增加引用计数
  release(&itable.lock); // 释放itable的锁
  return ip;
}

// Lock the given inode.
// Reads the inode from disk if necessary.
// 锁定给定的inode。
// 如果需要，会从磁盘读取inode数据。
void
ilock(struct inode *ip)
{
  struct buf *bp;
  struct dinode *dip;

  if(ip == 0 || ip->ref < 1) // 检查指针是否有效，引用计数是否至少为1
    panic("ilock");

  acquiresleep(&ip->lock); // 获取该inode的睡眠锁。如果锁被其他进程持有，则当前进程会睡眠。

  if(ip->valid == 0){ // 如果inode元数据还未从磁盘加载到内存中（无效状态）
    bp = bread(ip->dev, IBLOCK(ip->inum, sb)); // 从磁盘读取该inode所在的块
    dip = (struct dinode*)bp->data + ip->inum%IPB; // 定位到块内具体的dinode结构
    ip->type = dip->type; // 将磁盘上的元数据拷贝到内存inode结构中
    ip->major = dip->major;
    ip->minor = dip->minor;
    ip->nlink = dip->nlink;
    ip->size = dip->size;
    memmove(ip->addrs, dip->addrs, sizeof(ip->addrs));
    brelse(bp); // 释放缓冲区
    ip->valid = 1; // 标记内存中的inode数据现在是有效的
    if(ip->type == 0) // 如果从磁盘读出的inode类型是0（未分配），这是个错误
      panic("ilock: no type");
  }
}

// Unlock the given inode.
// 解锁给定的inode。
void
iunlock(struct inode *ip)
{
  if(ip == 0 || !holdingsleep(&ip->lock) || ip->ref < 1) // 检查指针有效性、是否持有锁、引用计数是否有效
    panic("iunlock");

  releasesleep(&ip->lock); // 释放睡眠锁
}

// 减少对一个内存inode的引用。
// 如果这是最后一个引用，那么这个inode缓存条目可以被回收。
// 如果这是最后一个引用，并且该inode没有链接指向它，那么就释放磁盘上的inode（及其内容）。
// 所有对iput()的调用都必须在一个事务中，以防需要释放inode。
void
iput(struct inode *ip)
{
  acquire(&itable.lock); // 获取itable锁，准备修改引用计数

  // 检查是否满足释放条件：
  // ip->ref == 1: 当前是最后一个使用它的内存引用
  // ip->valid:   内存中的数据是有效的（如果无效，说明可能没从磁盘读，或者数据已过时）
  // ip->nlink == 0: 磁盘上没有任何目录项指向它
  if(ip->ref == 1 && ip->valid && ip->nlink == 0){
    // inode has no links and no other references: truncate and free.
    // inode没有链接也没有其他引用：截断并释放。

    // ip->ref == 1 means no other process can have ip locked,
    // so this acquiresleep() won't block (or deadlock).
    // ref等于1意味着没有其他进程可以持有该inode的锁，所以这里的acquiresleep不会阻塞（或死锁）。
    acquiresleep(&ip->lock);

    release(&itable.lock); // 释放itable锁，因为接下来要执行可能耗时的磁盘操作

    itrunc(ip); // 截断文件，释放所有数据块
    ip->type = 0; // 将inode类型设为0，标记为可用
    iupdate(ip); // 将这些修改写回磁盘
    ip->valid = 0; // 标记内存中的数据不再有效

    releasesleep(&ip->lock); // 释放inode的睡眠锁

    acquire(&itable.lock); // 重新获取itable锁，准备修改引用计数
  }

  ip->ref--; // 减少引用计数
  release(&itable.lock); // 释放itable锁
}

// Common idiom: unlock, then put.
// 常见用法：先解锁，然后释放引用。
void
iunlockput(struct inode *ip)
{
  iunlock(ip); // 解锁
  iput(ip); // 释放引用
}

// ireclaim函数扫描磁盘上的inode，回收所有孤立的inode。
// 孤立的inode指的是一个被标记为已分配，但没有任何目录项指向它（nlink==0）的inode。
// 这种情况可能在系统创建了一个inode之后、但还未将其链接到目录之前崩溃时发生。
void
ireclaim(int dev)
{
  // 遍历磁盘上所有的 inode
  for (int inum = 1; inum < sb.ninodes; inum++) {
    struct inode *ip = 0;
    // 读取包含当前 inode 的磁盘块
    struct buf *bp = bread(dev, IBLOCK(inum, sb));
    // 定位到块中具体的 dinode 结构
    struct dinode *dip = (struct dinode *)bp->data + inum % IPB;
    // 检查是否为孤立 inode (类型不为0表示已分配，链接数为0表示无目录项指向)
    if (dip->type != 0 && dip->nlink == 0) {  // is an orphaned inode
      printf("ireclaim: orphaned inode %d\n", inum); // 打印信息
      ip = iget(dev, inum); // 获取该 inode 的内存缓存
    }
    brelse(bp); // 释放缓冲区
    if (ip) { // 如果找到了一个孤立的 inode
      begin_op(); // 开始一个文件系统事务
      ilock(ip);  // 锁定它
      iunlock(ip); // 立即解锁（因为我们只是为了触发iput的逻辑）
      iput(ip);   // 调用iput，因为nlink为0且这将是最后一个ref，会触发itrunc和释放
      end_op();   // 结束事务
    }
  }
}


// inode内容管理
//
// 每个inode关联的内容（数据）存储在磁盘上的块中。
// 前NDIRECT个块的块号直接列在ip->addrs[]数组中。
// 接下来的NINDIRECT个块的块号列在ip->addrs[NDIRECT]所指向的间接块中。
//
// 返回inode ip中第n个逻辑块的磁盘块地址。
// 如果这个块不存在，bmap会分配一个。
// 如果磁盘空间不足，返回0。
static uint
bmap(struct inode *ip, uint bn)
{
  uint addr, *a;
  struct buf *bp;

  if(bn < NDIRECT){ // 如果请求的块号在前 NDIRECT 个直接块的范围内
    if((addr = ip->addrs[bn]) == 0){ // 如果这个直接块指针为空（即还未分配）
      addr = balloc(ip->dev); // 分配一个新的数据块
      if(addr == 0) // 如果分配失败
        return 0;
      ip->addrs[bn] = addr; // 将新块的地址存入inode的直接块指针
    }
    return addr; // 返回块地址
  }
  bn -= NDIRECT; // 减去直接块的数量，得到在间接块中的索引

  if(bn < NINDIRECT){ // 如果块号在间接块的范围内
    // 加载间接块，如果需要则先分配。
    if((addr = ip->addrs[NDIRECT]) == 0){ // 如果间接块本身还未分配
      addr = balloc(ip->dev); // 分配一个块作为间接块
      if(addr == 0) // 如果分配失败
        return 0;
      ip->addrs[NDIRECT] = addr; // 将间接块的地址存入inode
    }
    bp = bread(ip->dev, addr); // 读取间接块
    a = (uint*)bp->data; // 将间接块的数据解释为地址数组
    if((addr = a[bn]) == 0){ // 如果间接块中对应的条目为空（即数据块还未分配）
      addr = balloc(ip->dev); // 分配一个新的数据块
      if(addr){ // 如果分配成功
        a[bn] = addr; // 将新数据块的地址写入间接块
        log_write(bp); // 将对间接块的修改写入日志
      }
    }
    brelse(bp); // 释放间接块的缓冲区
    return addr; // 返回数据块的地址
  }

  panic("bmap: out of range"); // 如果bn超出了所有范围，说明逻辑错误，系统崩溃
}

// 截断inode（丢弃其所有内容）。
// 调用者必须持有ip->lock。
void
itrunc(struct inode *ip)
{
  int i, j;
  struct buf *bp;
  uint *a;

  // 释放所有直接数据块
  for(i = 0; i < NDIRECT; i++){
    if(ip->addrs[i]){
      bfree(ip->dev, ip->addrs[i]);
      ip->addrs[i] = 0;
    }
  }

  // 释放所有间接数据块
  if(ip->addrs[NDIRECT]){
    bp = bread(ip->dev, ip->addrs[NDIRECT]); // 读取间接块
    a = (uint*)bp->data; // 将其内容视为地址数组
    for(j = 0; j < NINDIRECT; j++){
      if(a[j])
        bfree(ip->dev, a[j]); // 释放每个地址指向的数据块
    }
    brelse(bp); // 释放间接块的缓冲区
    bfree(ip->dev, ip->addrs[NDIRECT]); // 释放间接块本身
    ip->addrs[NDIRECT] = 0;
  }

  ip->size = 0; // 文件大小设为0
  iupdate(ip); // 将inode的修改（清空的地址和大小）写回磁盘
}

// 从inode拷贝状态信息到stat结构体。
// 调用者必须持有ip->lock。
void
stati(struct inode *ip, struct stat *st)
{
  st->dev = ip->dev; // 设备号
  st->ino = ip->inum; // inode号
  st->type = ip->type; // 文件类型
  st->nlink = ip->nlink; // 链接数
  st->size = ip->size; // 文件大小（字节）
}

// 从inode读取数据。
// 调用者必须持有ip->lock。
// 如果user_dst为1，dst是一个用户虚拟地址；否则，dst是一个内核地址。
int
readi(struct inode *ip, int user_dst, uint64 dst, uint off, uint n)
{
  uint tot, m;
  struct buf *bp;

  if(off > ip->size || off + n < off) // 检查偏移量和读取长度是否有效
    return 0;
  if(off + n > ip->size) // 如果读取范围超出文件末尾
    n = ip->size - off; // 调整读取长度，最多读到文件末尾

  // 循环读取，直到读完n个字节
  for(tot=0; tot<n; tot+=m, off+=m, dst+=m){
    uint addr = bmap(ip, off/BSIZE); // 获取当前偏移量所在逻辑块对应的物理块地址
    if(addr == 0) // 如果获取地址失败
      break;
    bp = bread(ip->dev, addr); // 读取该物理块
    m = min(n - tot, BSIZE - off%BSIZE); // 计算本次循环可以读取的字节数
    // m是“剩余要读的字节数”和“当前块内剩余的字节数”中的较小值
    // 将数据从缓冲区拷贝到目标地址（用户空间或内核空间）
    if(either_copyout(user_dst, dst, bp->data + (off % BSIZE), m) == -1) {
      brelse(bp); // 拷贝失败，释放缓冲区
      tot = -1; // 标记为错误
      break;
    }
    brelse(bp); // 释放缓冲区
  }
  return tot; // 返回成功读取的字节数，或-1表示错误
}

// 向inode写入数据。
// 调用者必须持有ip->lock。
// 如果user_src为1，src是一个用户虚拟地址；否则，src是一个内核地址。
// 返回成功写入的字节数。
// 如果返回值小于请求的n，则表示发生了某种错误。
int
writei(struct inode *ip, int user_src, uint64 src, uint off, uint n)
{
  uint tot, m;
  struct buf *bp;

  if(off > ip->size || off + n < off) // 检查偏移量和写入长度是否有效
    return -1;
  if(off + n > MAXFILE*BSIZE) // 检查写入是否会超过文件最大尺寸
    return -1;

  // 循环写入，直到写完n个字节
  for(tot=0; tot<n; tot+=m, off+=m, src+=m){
    uint addr = bmap(ip, off/BSIZE); // 获取当前偏移量所在逻辑块对应的物理块地址（如果不存在会分配）
    if(addr == 0) // 如果块分配失败
      break;
    bp = bread(ip->dev, addr); // 读取该物理块
    m = min(n - tot, BSIZE - off%BSIZE); // 计算本次循环可以写入的字节数
    // 将数据从源地址（用户空间或内核空间）拷贝到缓冲区
    if(either_copyin(bp->data + (off % BSIZE), user_src, src, m) == -1) {
      brelse(bp); // 拷贝失败，释放缓冲区
      break;
    }
    log_write(bp); // 将对缓冲区的修改（即写入）记录到日志
    brelse(bp); // 释放缓冲区
  }

  if(off > ip->size) // 如果写入操作扩展了文件
    ip->size = off; // 更新文件大小

  // 即使文件大小没有改变，也要将inode写回磁盘，
  // 因为上面的循环可能调用了bmap()并为ip->addrs[]添加了新的块。
  iupdate(ip);

  return tot; // 返回成功写入的字节数
}

// Directories
// 目录管理部分

int
namecmp(const char *s, const char *t)
{
  return strncmp(s, t, DIRSIZ); // 比较两个文件名，最多比较DIRSIZ个字符
}

// 在一个目录中查找一个目录项。
// 如果找到，将*poff设置为该目录项的字节偏移量。
struct inode*
dirlookup(struct inode *dp, char *name, uint *poff)
{
  uint off, inum;
  struct dirent de;

  if(dp->type != T_DIR) // 确保dp确实是一个目录
    panic("dirlookup not DIR");

  // 遍历目录文件的所有目录项
  for(off = 0; off < dp->size; off += sizeof(de)){
    // 读取一个目录项的数据
    if(readi(dp, 0, (uint64)&de, off, sizeof(de)) != sizeof(de))
      panic("dirlookup read");
    if(de.inum == 0) // 如果目录项的inum为0，表示这是一个空闲的目录项
      continue;
    if(namecmp(name, de.name) == 0){ // 如果文件名匹配
      // 目录项与路径元素匹配
      if(poff) // 如果poff非空
        *poff = off; // 记录下这个目录项的偏移量
      inum = de.inum; // 获取该目录项对应的inode号
      return iget(dp->dev, inum); // 获取该inode的内存表示并返回
    }
  }

  return 0; // 如果没找到，返回NULL
}

// 将一个新的目录项（文件名、inode号）写入目录dp中。
// 成功返回0，失败（如磁盘块不足）返回-1。
int
dirlink(struct inode *dp, char *name, uint inum)
{
  int off;
  struct dirent de;
  struct inode *ip;

  // Check that name is not present.
  // 检查文件名是否已经存在
  if((ip = dirlookup(dp, name, 0)) != 0){
    iput(ip); // 如果存在，释放查找到的inode，返回错误
    return -1;
  }

  // Look for an empty dirent.
  // 寻找一个空的目录项
  for(off = 0; off < dp->size; off += sizeof(de)){
    if(readi(dp, 0, (uint64)&de, off, sizeof(de)) != sizeof(de))
      panic("dirlink read");
    if(de.inum == 0) // 找到了一个inum为0的空目录项
      break;
  }

  strncpy(de.name, name, DIRSIZ); // 拷贝文件名
  de.inum = inum; // 设置inode号
  // 将这个新的目录项写回磁盘
  if(writei(dp, 0, (uint64)&de, off, sizeof(de)) != sizeof(de))
    return -1; // 写入失败

  return 0; // 成功
}

// Paths
// 路径解析部分
//
// 从path中拷贝下一个路径元素到name中。
// 返回一个指向被拷贝元素之后部分的指针。
// 返回的路径没有前导斜杠，所以调用者可以检查*path=='\0'来判断这是否是最后一个元素。
// 如果没有可提取的名称，返回0。
static char*
skipelem(char *path, char *name)
{
  char *s;
  int len;

  while(*path == '/') // 跳过所有前导斜杠
    path++;
  if(*path == 0) // 如果路径已经为空
    return 0;
  s = path; // 记录路径元素的开始位置
  while(*path != '/' && *path != 0) // 向前扫描直到遇到下一个斜杠或路径末尾
    path++;
  len = path - s; // 计算路径元素的长度
  if(len >= DIRSIZ) // 如果长度超限
    memmove(name, s, DIRSIZ); // 拷贝DIRSIZ个字节
  else {
    memmove(name, s, len); // 拷贝路径元素
    name[len] = 0; // 添加字符串结束符
  }
  while(*path == '/') // 跳过元素之后的所有斜杠
    path++;
  return path; // 返回剩余的路径
}

// 查找并返回一个路径名对应的inode。
// 如果parent不为0，则返回父目录的inode，并将最后一个路径元素拷贝到name中。
// 必须在事务中调用，因为它会调用iput()。
static struct inode*
namex(char *path, int nameiparent, char *name)
{
  struct inode *ip, *next;

  if(*path == '/') // 如果是绝对路径
    ip = iget(ROOTDEV, ROOTINO); // 从根目录inode开始
  else // 否则是相对路径
    ip = idup(myproc()->cwd); // 从当前工作目录的inode开始

  // 循环解析路径的每个部分
  while((path = skipelem(path, name)) != 0){
    ilock(ip); // 锁定当前目录inode
    if(ip->type != T_DIR){ // 如果当前inode不是一个目录
      iunlockput(ip); // 解锁并释放，返回失败
      return 0;
    }
    if(nameiparent && *path == '\0'){ // 如果需要返回父目录，并且这已经是路径的最后一部分
      // Stop one level early.
      // 提前一层停止
      iunlock(ip); // 解锁当前inode（即父目录）
      return ip; // 返回父目录inode
    }
    if((next = dirlookup(ip, name, 0)) == 0){ // 在当前目录中查找下一个路径元素
      iunlockput(ip); // 如果没找到，解锁并释放，返回失败
      return 0;
    }
    iunlockput(ip); // 找到了，解锁并释放当前目录inode
    ip = next; // 进入下一级目录
  }
  if(nameiparent){ // 如果循环结束了，但要求的是父目录（说明路径类似"/"或"/a"，没有父目录）
    iput(ip); // 释放当前inode
    return 0; // 返回失败
  }
  return ip; // 返回最终找到的inode
}

// 查找并返回路径path对应的inode。
struct inode*
namei(char *path)
{
  char name[DIRSIZ];
  return namex(path, 0, name); // 调用namex，参数nameiparent为0，表示要找到路径本身
}

// 查找路径path的父目录，并把最后一个文件名存入name。
struct inode*
nameiparent(char *path, char *name)
{
  return namex(path, 1, name); // 调用namex，参数nameiparent为1，表示要找到路径的父目录
}