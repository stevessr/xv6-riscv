// 包含 xv6 系统所需的核心头文件
#include "types.h"      // 定义基本数据类型
#include "riscv.h"      // RISC-V 特定的宏和函数
#include "defs.h"       // 内核函数原型
#include "param.h"      // 系统参数
#include "spinlock.h"   // 自旋锁
#include "proc.h"       // 进程相关
#include "fs.h"         // 文件系统
#include "sleeplock.h"  // 睡眠锁
#include "file.h"       // 文件操作

// 定义管道缓冲区的大小。这是一个循环缓冲区。
#define PIPESIZE 512

// 管道结构体
struct pipe {
  struct spinlock lock; // 保护管道的自旋锁
  char data[PIPESIZE];  // 存储管道数据的缓冲区
  uint nread;           // 从管道中读取的字节数
  uint nwrite;          // 写入管道的字节数
  int readopen;         // 读端是否打开 (1 表示打开, 0 表示关闭)
  int writeopen;        // 写端是否打开 (1 表示打开, 0 表示关闭)
};

// 分配一个管道，并返回两个文件描述符（一个用于读取，一个用于写入）。
// f0: 用于读取的文件描述符
// f1: 用于写入的文件描述符
int
pipealloc(struct file **f0, struct file **f1)
{
  struct pipe *pi;

  pi = 0;
  *f0 = *f1 = 0;
  // 为管道的读端和写端分别分配一个文件结构体
  if((*f0 = filealloc()) == 0 || (*f1 = filealloc()) == 0)
    goto bad; // 如果分配失败，跳转到错误处理
  // 为管道本身分配内存
  if((pi = (struct pipe*)kalloc()) == 0)
    goto bad; // 如果分配失败，跳转到错误处理
  
  // 初始化管道状态
  pi->readopen = 1;     // 读端打开
  pi->writeopen = 1;    // 写端打开
  pi->nwrite = 0;       // 已写入 0 字节
  pi->nread = 0;        // 已读取 0 字节
  initlock(&pi->lock, "pipe"); // 初始化自旋锁

  // 配置读文件描述符
  (*f0)->type = FD_PIPE;      // 文件类型为管道
  (*f0)->readable = 1;        // 可读
  (*f0)->writable = 0;        // 不可写
  (*f0)->pipe = pi;           // 指向管道结构体

  // 配置写文件描述符
  (*f1)->type = FD_PIPE;      // 文件类型为管道
  (*f1)->readable = 0;        // 不可读
  (*f1)->writable = 1;        // 可写
  (*f1)->pipe = pi;           // 指向管道结构体

  return 0; // 成功

bad: // 错误处理标签
  if(pi)
    kfree((char*)pi); // 如果管道已分配，则释放它
  if(*f0)
    fileclose(*f0);   // 如果读文件描述符已分配，则关闭它
  if(*f1)
    fileclose(*f1);   // 如果写文件描述符已分配，则关闭它
  return -1; // 返回错误
}

// 关闭管道的一端（读或写）。
// pi: 指向管道的指针
// writable: 指示被关闭的是否是写端
void
pipeclose(struct pipe *pi, int writable)
{
  acquire(&pi->lock); // 获取管道锁

  if(writable){ // 如果关闭的是写端
    pi->writeopen = 0;  // 标记写端已关闭
    wakeup(&pi->nread); // 唤醒可能正在等待数据的读进程
  } else { // 如果关闭的是读端
    pi->readopen = 0;   // 标记读端已关闭
    wakeup(&pi->nwrite);// 唤醒可能正在等待空间的写进程
  }

  // 如果读端和写端都已关闭，则释放管道资源
  if(pi->readopen == 0 && pi->writeopen == 0){
    release(&pi->lock); // 释放锁
    kfree((char*)pi);   // 释放管道结构体占用的内存
  } else {
    release(&pi->lock); // 否则只释放锁
  }
}

// 从用户空间地址 addr 处向管道写入 n 个字节。
// pi: 指向管道的指针
// addr: 用户空间源数据地址
// n: 要写入的字节数
int
pipewrite(struct pipe *pi, uint64 addr, int n)
{
  int i = 0;
  struct proc *pr = myproc(); // 获取当前进程

  acquire(&pi->lock); // 获取管道锁

  while(i < n){ // 循环直到写入 n 个字节
    // 如果读端已关闭或当前进程已被杀死，则无法写入
    if(pi->readopen == 0 || killed(pr)){
      release(&pi->lock);
      return -1; // 返回错误
    }
    
    // 检查管道是否已满。nwrite 和 nread 的差值表示缓冲区中的数据量。
    if(pi->nwrite == pi->nread + PIPESIZE){
      // 管道已满，唤醒可能在等待数据的读进程
      wakeup(&pi->nread);
      // 让当前写进程休眠，等待读进程读取数据后唤醒
      sleep(&pi->nwrite, &pi->lock);
    } else {
      char ch;
      // 从用户空间拷贝一个字节到内核
      if(copyin(pr->pagetable, &ch, addr + i, 1) == -1)
        break; // 如果拷贝失败，则中断写入
      // 将字节写入管道的循环缓冲区
      pi->data[pi->nwrite % PIPESIZE] = ch;
      pi->nwrite++; // 增加写入字节数
      i++; // 增加已写入计数器
    }
  }

  wakeup(&pi->nread); // 写入完成后，唤醒可能在等待数据的读进程
  release(&pi->lock); // 释放管道锁

  return i; // 返回实际写入的字节数
}

// 从管道读取最多 n 个字节到用户空间地址 addr。
// pi: 指向管道的指针
// addr: 用户空间目标地址
// n: 要读取的字节数
int
piperead(struct pipe *pi, uint64 addr, int n)
{
  int i;
  struct proc *pr = myproc(); // 获取当前进程
  char ch;

  acquire(&pi->lock); // 获取管道锁

  // 当管道为空（nread == nwrite）并且写端仍然打开时，等待数据。
  // 如果写端关闭，则管道为空时读取将直接返回 0。
  while(pi->nread == pi->nwrite && pi->writeopen){
    // 如果当前进程被杀死，则无法读取
    if(killed(pr)){
      release(&pi->lock);
      return -1; // 返回错误
    }
    // 让当前读进程休眠，等待写进程写入数据后唤醒
    sleep(&pi->nread, &pi->lock);
  }

  // 循环读取数据，最多读取 n 个字节
  for(i = 0; i < n; i++){
    // 如果管道为空，则停止读取
    if(pi->nread == pi->nwrite)
      break;
    
    // 从管道的循环缓冲区中读取一个字节
    ch = pi->data[pi->nread % PIPESIZE];
    pi->nread++; // 增加读取字节数

    // 将读取的字节拷贝到用户空间
    if(copyout(pr->pagetable, addr + i, &ch, 1) == -1)
      break; // 如果拷贝失败，则中断读取
  }

  wakeup(&pi->nwrite); // 读取完成后，唤醒可能在等待空间的写进程
  release(&pi->lock);  // 释放管道锁

  return i; // 返回实际读取的字节数
}
