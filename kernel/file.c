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

// 设备驱动表，`devsw[i]` 包含主设备号 `i` 的驱动函数
struct devsw devsw[NDEV];

// 全局文件表 (ftable)。
// 这是系统中所有打开文件的池。
// `ftable.lock` 保护该表，确保并发访问的安全性。
// `ftable.file` 是一个包含 `NFILE` 个 `struct file` 的数组。
struct {
  struct spinlock lock;
  struct file file[NFILE];
} ftable;

// 初始化全局文件表锁
void
fileinit(void)
{
  initlock(&ftable.lock, "ftable");
}

// 分配一个文件结构体 (struct file)。
// 这是在内核中打开一个新文件的第一步。
// 它会在全局文件表 `ftable` 中查找一个未被使用的 `struct file`。
// @return: 成功时返回一个指向 `struct file` 的指针，失败时返回 0。
struct file*
filealloc(void)
{
  struct file *f;

  acquire(&ftable.lock);
  for(f = ftable.file; f < ftable.file + NFILE; f++){
    if(f->ref == 0){ // 找到一个空闲的文件结构体 (引用计数为 0)
      f->ref = 1;      // 将其标记为已使用 (引用计数设为 1)
      release(&ftable.lock);
      return f;
    }
  }
  release(&ftable.lock);
  return 0; // 没有可用的文件结构体
}

// 增加文件 f 的引用计数。
// 当一个文件描述符被复制时（例如 `dup()` 或 `fork()`），会调用此函数。
// 这允许多个文件描述符指向同一个 `struct file`。
// @param f: 要增加引用计数的文件结构体。
// @return: 返回指向同一个文件结构体的指针 `f`。
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

// 关闭文件 f。
// 这会减少文件的引用计数。如果引用计数降至 0，
// 则会释放底层的资源（如 inode 或管道）。
// @param f: 要关闭的文件结构体。
void
fileclose(struct file *f)
{
  struct file ff; // 用于临时保存文件信息的副本

  acquire(&ftable.lock);
  if(f->ref < 1)
    panic("fileclose");
  if(--f->ref > 0){ // 如果还有其他引用，则仅减少引用计数
    release(&ftable.lock);
    return;
  }
  // 如果引用计数为 0，则这是最后一个引用，需要真正关闭文件
  ff = *f;
  f->ref = 0;
  f->type = FD_NONE; // 重置文件类型
  release(&ftable.lock);

  if(ff.type == FD_PIPE){
    // 如果是管道文件，关闭管道
    pipeclose(ff.pipe, ff.writable);
  } else if(ff.type == FD_INODE || ff.type == FD_DEVICE){
    // 如果是 inode 或设备文件，释放 inode
    // `begin_op`/`end_op` 用于确保 inode 操作的原子性
    begin_op();
    iput(ff.ip);
    end_op();
  }
}

// 获取文件 f 的状态信息 (元数据)。
// @param f: 要获取状态的文件。
// @param addr: 指向用户空间 `struct stat` 的地址，用于存储结果。
// @return: 成功时返回 0，失败时返回 -1。
int
filestat(struct file *f, uint64 addr)
{
  struct proc *p = myproc();
  struct stat st;
  
  if(f->type == FD_INODE || f->type == FD_DEVICE){
    ilock(f->ip);
    stati(f->ip, &st); // 从 inode 中读取状态信息
    iunlock(f->ip);
    // 将状态信息复制到用户空间
    if(copyout(p->pagetable, addr, (char *)&st, sizeof(st)) < 0)
      return -1;
    return 0;
  }
  return -1;
}

// 从文件 f 读取数据。
// @param f: 要读取的文件。
// @param addr: 存储读取数据的用户空间缓冲区的地址。
// @param n: 要读取的字节数。
// @return: 返回读取的字节数；如果出错则返回 -1。
int
fileread(struct file *f, uint64 addr, int n)
{
  int r = 0;

  if(f->readable == 0)
    return -1;

  if(f->type == FD_PIPE){
    r = piperead(f->pipe, addr, n);
  } else if(f->type == FD_DEVICE){
    if(f->major < 0 || f->major >= NDEV || !devsw[f->major].read)
      return -1;
    r = devsw[f->major].read(1, addr, n);
  } else if(f->type == FD_INODE){
    ilock(f->ip);
    // 从 inode 读取数据，并更新文件偏移量
    if((r = readi(f->ip, 1, addr, f->off, n)) > 0)
      f->off += r;
    iunlock(f->ip);
  } else {
    panic("fileread");
  }

  return r;
}

// 向文件 f 写入数据。
// @param f: 要写入的文件。
// @param addr: 包含要写入数据的用户空间缓冲区的地址。
// @param n: 要写入的字节数。
// @return: 返回写入的字节数；如果出错则返回 -1。
int
filewrite(struct file *f, uint64 addr, int n)
{
  int r, ret = 0;

  if(f->writable == 0)
    return -1;

  if(f->type == FD_PIPE){
    ret = pipewrite(f->pipe, addr, n);
  } else if(f->type == FD_DEVICE){
    if(f->major < 0 || f->major >= NDEV || !devsw[f->major].write)
      return -1;
    ret = devsw[f->major].write(1, addr, n);
  } else if(f->type == FD_INODE){
    // 为了避免超出单个日志事务的大小，分块写入。
    // `MAXOPBLOCKS` 限制了单个事务中可以包含的块数。
    int max = ((MAXOPBLOCKS-1-1-2) / 2) * BSIZE;
    int i = 0;
    while(i < n){
      int n1 = n - i;
      if(n1 > max)
        n1 = max;

      begin_op();
      ilock(f->ip);
      // 向 inode 写入数据，并更新文件偏移量
      if ((r = writei(f->ip, 1, addr + i, f->off, n1)) > 0)
        f->off += r;
      iunlock(f->ip);
      end_op();

      if(r != n1){
        // `writei` 发生错误或写入的字节数少于预期
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
