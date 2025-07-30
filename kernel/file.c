//
// 为涉及文件描述符的系统调用提供支持函数。
//

#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "param.h"
#include "fs.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "file.h"
#include "stat.h"
#include "proc.h"

struct devsw devsw[NDEV]; // 设备驱动表
struct {
  struct spinlock lock;     // 保护文件表
  struct file file[NFILE];  // 文件表
} ftable;

void
fileinit(void)
{
  initlock(&ftable.lock, "ftable"); // 初始化文件表锁
}

// 分配一个文件结构体。
struct file*
filealloc(void)
{
  struct file *f;

  acquire(&ftable.lock);
  for(f = ftable.file; f < ftable.file + NFILE; f++){
    if(f->ref == 0){ // 找到一个空闲的文件结构体
      f->ref = 1; // 增加引用计数
      release(&ftable.lock);
      return f;
    }
  }
  release(&ftable.lock);
  return 0; // 没有可用的文件结构体
}

// 增加文件 f 的引用计数。
struct file*
filedup(struct file *f)
{
  acquire(&ftable.lock);
  if(f->ref < 1)
    panic("filedup");
  f->ref++;
  release(&ftable.lock);
  return f;
}

// 关闭文件 f。（减少引用计数，当引用计数为 0 时关闭。）
void
fileclose(struct file *f)
{
  struct file ff;

  acquire(&ftable.lock);
  if(f->ref < 1)
    panic("fileclose");
  if(--f->ref > 0){ // 还有其他引用，直接返回
    release(&ftable.lock);
    return;
  }
  // 引用计数为 0，真正关闭文件
  ff = *f;
  f->ref = 0;
  f->type = FD_NONE;
  release(&ftable.lock);

  if(ff.type == FD_PIPE){ // 如果是管道文件
    pipeclose(ff.pipe, ff.writable);
  } else if(ff.type == FD_INODE || ff.type == FD_DEVICE){ // 如果是 inode 或设备文件
    begin_op();
    iput(ff.ip); // 释放 inode
    end_op();
  }
}

// 获取文件 f 的元数据。
// addr 是一个用户虚拟地址，指向一个 struct stat。
int
filestat(struct file *f, uint64 addr)
{
  struct proc *p = myproc();
  struct stat st;
  
  if(f->type == FD_INODE || f->type == FD_DEVICE){
    ilock(f->ip);
    stati(f->ip, &st); // 获取 inode 的状态信息
    iunlock(f->ip);
    // 将状态信息复制到用户空间
    if(copyout(p->pagetable, addr, (char *)&st, sizeof(st)) < 0)
      return -1;
    return 0;
  }
  return -1;
}

// 从文件 f 读取数据。
// addr 是一个用户虚拟地址。
int
fileread(struct file *f, uint64 addr, int n)
{
  int r = 0;

  if(f->readable == 0) // 文件不可读
    return -1;

  if(f->type == FD_PIPE){
    r = piperead(f->pipe, addr, n);
  } else if(f->type == FD_DEVICE){
    if(f->major < 0 || f->major >= NDEV || !devsw[f->major].read)
      return -1;
    r = devsw[f->major].read(1, addr, n);
  } else if(f->type == FD_INODE){
    ilock(f->ip);
    if((r = readi(f->ip, 1, addr, f->off, n)) > 0)
      f->off += r; // 更新文件偏移量
    iunlock(f->ip);
  } else {
    panic("fileread");
  }

  return r;
}

// 向文件 f 写入数据。
// addr 是一个用户虚拟地址。
int
filewrite(struct file *f, uint64 addr, int n)
{
  int r, ret = 0;

  if(f->writable == 0) // 文件不可写
    return -1;

  if(f->type == FD_PIPE){
    ret = pipewrite(f->pipe, addr, n);
  } else if(f->type == FD_DEVICE){
    if(f->major < 0 || f->major >= NDEV || !devsw[f->major].write)
      return -1;
    ret = devsw[f->major].write(1, addr, n);
  } else if(f->type == FD_INODE){
    // 为了避免超出最大日志事务大小，一次写入几个块。
    // 这包括 i-node、间接块、分配块，
    // 以及为非对齐写入准备的 2 个块的余量。
    // 这个逻辑实际上应该放在更底层，因为 writei()
    // 可能正在写入像控制台这样的设备。
    int max = ((MAXOPBLOCKS-1-1-2) / 2) * BSIZE;
    int i = 0;
    while(i < n){
      int n1 = n - i;
      if(n1 > max)
        n1 = max;

      begin_op();
      ilock(f->ip);
      if ((r = writei(f->ip, 1, addr + i, f->off, n1)) > 0)
        f->off += r; // 更新文件偏移量
      iunlock(f->ip);
      end_op();

      if(r != n1){
        // writei 出错
        break;
      }
      i += r;
    }
    ret = (i == n ? n : -1);
  } else {
    panic("filewrite");
  }

  return ret;
}
