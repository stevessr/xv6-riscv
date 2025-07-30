#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "param.h"
#include "spinlock.h"
#include "proc.h"
#include "fs.h"
#include "sleeplock.h"
#include "file.h"

#define PIPESIZE 512 // 管道缓冲区大小

struct pipe {
  struct spinlock lock;
  char data[PIPESIZE];
  uint nread;     // 已读取的字节数
  uint nwrite;    // 已写入的字节数
  int readopen;   // 读文件描述符仍然打开
  int writeopen;  // 写文件描述符仍然打开
};

// 分配管道
int
pipealloc(struct file **f0, struct file **f1)
{
  struct pipe *pi;

  pi = 0;
  *f0 = *f1 = 0;
  // 为读写两端分配文件结构
  if((*f0 = filealloc()) == 0 || (*f1 = filealloc()) == 0)
    goto bad;
  // 分配管道结构
  if((pi = (struct pipe*)kalloc()) == 0)
    goto bad;
  pi->readopen = 1;
  pi->writeopen = 1;
  pi->nwrite = 0;
  pi->nread = 0;
  initlock(&pi->lock, "pipe");
  // 设置读文件描述符
  (*f0)->type = FD_PIPE;
  (*f0)->readable = 1;
  (*f0)->writable = 0;
  (*f0)->pipe = pi;
  // 设置写文件描述符
  (*f1)->type = FD_PIPE;
  (*f1)->readable = 0;
  (*f1)->writable = 1;
  (*f1)->pipe = pi;
  return 0;

 bad: // 错误处理
  if(pi)
    kfree((char*)pi);
  if(*f0)
    fileclose(*f0);
  if(*f1)
    fileclose(*f1);
  return -1;
}

// 关闭管道
void
pipeclose(struct pipe *pi, int writable)
{
  acquire(&pi->lock);
  if(writable){ // 关闭写端
    pi->writeopen = 0;
    wakeup(&pi->nread); // 唤醒读端
  } else { // 关闭读端
    pi->readopen = 0;
    wakeup(&pi->nwrite); // 唤醒写端
  }
  if(pi->readopen == 0 && pi->writeopen == 0){ // 如果读写端都关闭了
    release(&pi->lock);
    kfree((char*)pi); // 释放管道
  } else
    release(&pi->lock);
}

// 向管道写入数据
int
pipewrite(struct pipe *pi, uint64 addr, int n)
{
  int i = 0;
  struct proc *pr = myproc();

  acquire(&pi->lock);
  while(i < n){
    if(pi->readopen == 0 || killed(pr)){ // 如果读端已关闭或进程被杀死
      release(&pi->lock);
      return -1;
    }
    if(pi->nwrite == pi->nread + PIPESIZE){ // 管道已满
      wakeup(&pi->nread); // 唤醒读端
      sleep(&pi->nwrite, &pi->lock); // 等待读端读取数据
    } else {
      char ch;
      if(copyin(pr->pagetable, &ch, addr + i, 1) == -1) // 从用户空间拷贝数据
        break;
      pi->data[pi->nwrite++ % PIPESIZE] = ch; // 写入数据
      i++;
    }
  }
  wakeup(&pi->nread); // 唤醒读端
  release(&pi->lock);

  return i;
}

// 从管道读取数据
int
piperead(struct pipe *pi, uint64 addr, int n)
{
  int i;
  struct proc *pr = myproc();
  char ch;

  acquire(&pi->lock);
  while(pi->nread == pi->nwrite && pi->writeopen){  // 管道为空且写端还打开着
    if(killed(pr)){ // 如果进程被杀死
      release(&pi->lock);
      return -1;
    }
    sleep(&pi->nread, &pi->lock); // 等待写端写入数据
  }
  for(i = 0; i < n; i++){
    if(pi->nread == pi->nwrite) // 管道为空
      break;
    ch = pi->data[pi->nread++ % PIPESIZE]; // 读取数据
    if(copyout(pr->pagetable, addr + i, &ch, 1) == -1) // 将数据拷贝到用户空间
      break;
  }
  wakeup(&pi->nwrite);  // 唤醒写端
  release(&pi->lock);
  return i;
}
