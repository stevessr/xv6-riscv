//
// 文件系统相关的系统调用。
//
// 此文件中的函数是用户空间程序和内核文件系统之间的接口。
// 它们主要负责：
// 1. 从用户空间获取和验证系统调用参数（例如文件描述符、路径名、缓冲区地址）。
// 2. 调用 file.c 和 fs.c 中更底层的函数来执行实际的文件操作。
// 3. 处理文件描述符的分配和释放。
// 4. 管理文件系统操作的原子性（通过 begin_op/end_op）。
//

#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "param.h"
#include "stat.h"
#include "spinlock.h"
#include "proc.h"
#include "fs.h"
#include "sleeplock.h"
#include "file.h"
#include "fcntl.h"

// 从当前进程的系统调用参数中获取第 n 个整数作为文件描述符。
// 验证该文件描述符是否有效，并返回对应的 `struct file` 指针。
//
// @param n: 系统调用参数的索引。
// @param pfd: (可选) 一个整型指针，用于存储文件描述符的值。
// @param pf: (可选) 一个文件指针的指针，用于存储找到的 `struct file`。
// @return: 成功返回0，失败返回-1。
static int
argfd(int n, int *pfd, struct file **pf)
{
  int fd;
  struct file *f;

  // 从用户空间获取整数参数
  argint(n, &fd);
  // 检查文件描述符是否越界，或者对应的文件指针是否为空
  if(fd < 0 || fd >= NOFILE || (f=myproc()->ofile[fd]) == 0)
    return -1;
  // 如果提供了pfd指针，则将fd写入
  if(pfd)
    *pfd = fd;
  // 如果提供了pf指针，则将文件指针写入
  if(pf)
    *pf = f;
  return 0;
}

// 为给定的文件在当前进程中分配一个未使用的文件描述符。
// 将 `struct file` 指针存入进程的文件描述符表中。
//
// @param f: 需要分配文件描述符的文件结构体指针。
// @return: 成功则返回新的文件描述符，失败则返回-1。
static int
fdalloc(struct file *f)
{
  int fd;
  struct proc *p = myproc();

  // 遍历进程的文件描述符表，查找空闲位置
  for(fd = 0; fd < NOFILE; fd++){
    if(p->ofile[fd] == 0){
      p->ofile[fd] = f;
      return fd;
    }
  }
  return -1; // 没有可用的文件描述符
}

// sys_dup 系统调用：复制一个已有的文件描述符。
// 创建一个新的文件描述符，它与旧的描述符指向同一个打开的文件。
//
// @return: 成功则返回新的文件描述符，失败则返回-1。
uint64
sys_dup(void)
{
  struct file *f;
  int fd;

  // 获取第一个参数（即要复制的文件描述符）对应的文件结构
  if(argfd(0, 0, &f) < 0)
    return -1;
  // 为该文件分配一个新的文件描述符
  if((fd=fdalloc(f)) < 0)
    return -1;
  // 增加文件的引用计数，因为现在有两个描述符指向它
  filedup(f);
  return fd;
}

// sys_read 系统调用：从文件中读取数据。
//
// @return: 成功则返回读取的字节数，失败则返回-1。
uint64
sys_read(void)
{
  struct file *f;
  int n;
  uint64 p;

  // 获取参数：用户缓冲区地址(p)和要读取的字节数(n)
  argaddr(1, &p);
  argint(2, &n);
  // 获取文件描述符对应的文件结构
  if(argfd(0, 0, &f) < 0)
    return -1;
  // 调用底层文件读取函数
  return fileread(f, p, n);
}

// sys_write 系统调用：向文件中写入数据。
//
// @return: 成功则返回写入的字节数，失败则返回-1。
uint64
sys_write(void)
{
  struct file *f;
  int n;
  uint64 p;
  
  // 获取参数：用户缓冲区地址(p)和要写入的字节数(n)
  argaddr(1, &p);
  argint(2, &n);
  // 获取文件描述符对应的文件结构
  if(argfd(0, 0, &f) < 0)
    return -1;

  // 调用底层文件写入函数
  return filewrite(f, p, n);
}

// sys_close 系统调用：关闭一个文件描述符。
//
// @return: 成功返回0，失败返回-1。
uint64
sys_close(void)
{
  int fd;
  struct file *f;

  // 获取文件描述符和对应的文件结构
  if(argfd(0, &fd, &f) < 0)
    return -1;
  // 从进程的文件表中移除该文件描述符
  myproc()->ofile[fd] = 0;
  // 关闭文件（这会减少引用计数，如果计数为0，则会释放 inode）
  fileclose(f);
  return 0;
}

// sys_fstat 系统调用：获取文件的状态信息。
//
// @return: 成功返回0，失败返回-1。
uint64
sys_fstat(void)
{
  struct file *f;
  uint64 st; // 指向用户空间 struct stat 的指针

  // 获取用户空间的 stat 结构体地址
  argaddr(1, &st);
  // 获取文件描述符对应的文件结构
  if(argfd(0, 0, &f) < 0)
    return -1;
  // 调用底层函数获取文件状态
  return filestat(f, st);
}

// sys_link 系统调用：为文件创建一个新的硬链接。
//
// @return: 成功返回0，失败返回-1。
uint64
sys_link(void)
{
  char name[DIRSIZ], new[MAXPATH], old[MAXPATH];
  struct inode *dp, *ip;

  // 获取旧路径和新路径
  if(argstr(0, old, MAXPATH) < 0 || argstr(1, new, MAXPATH) < 0)
    return -1;

  begin_op(); // 开始文件系统操作，确保原子性
  if((ip = namei(old)) == 0){ // 查找旧路径的 inode
    end_op();
    return -1;
  }

  ilock(ip); // 锁定 inode
  if(ip->type == T_DIR){ // 不允许对目录创建硬链接
    iunlockput(ip);
    end_op();
    return -1;
  }

  ip->nlink++; // 增加 inode 的链接计数
  iupdate(ip); // 将 inode 的变更写回磁盘
  iunlock(ip);

  if((dp = nameiparent(new, name)) == 0) // 查找新路径的父目录 inode
    goto bad;
  ilock(dp);
  // 检查设备号是否一致，并在父目录中创建目录项
  if(dp->dev != ip->dev || dirlink(dp, name, ip->inum) < 0){
    iunlockput(dp);
    goto bad;
  }
  iunlockput(dp);
  iput(ip); // 释放对旧路径 inode 的引用

  end_op();

  return 0;

bad: // 链接失败，需要回滚
  ilock(ip);
  ip->nlink--; // 恢复链接计数
  iupdate(ip);
  iunlockput(ip);
  end_op();
  return -1;
}

// 检查目录 dp 是否为空（除了 "." 和 ".."）。
static int
isdirempty(struct inode *dp)
{
  int off;
  struct dirent de;

  // 遍历目录中的所有目录项
  // 跳过前两个条目，即 "." 和 ".."
  for(off=2*sizeof(de); off<dp->size; off+=sizeof(de)){
    // 读取目录项
    if(readi(dp, 0, (uint64)&de, off, sizeof(de)) != sizeof(de))
      panic("isdirempty: readi");
    // 如果 inode 号不为0，说明目录项有效，目录不为空
    if(de.inum != 0)
      return 0;
  }
  return 1; // 目录为空
}

// sys_unlink 系统调用：删除一个文件链接。
// 如果文件的链接数降为0，则释放文件数据和 inode。
//
// @return: 成功返回0，失败返回-1。
uint64
sys_unlink(void)
{
  struct inode *ip, *dp;
  struct dirent de;
  char name[DIRSIZ], path[MAXPATH];
  uint off;

  if(argstr(0, path, MAXPATH) < 0)
    return -1;

  begin_op();
  if((dp = nameiparent(path, name)) == 0){ // 查找父目录
    end_op();
    return -1;
  }

  ilock(dp);

  // 不允许删除 "." 或 ".."
  if(namecmp(name, ".") == 0 || namecmp(name, "..") == 0)
    goto bad_unlink;

  if((ip = dirlookup(dp, name, &off)) == 0) // 在父目录中查找要删除的文件
    goto bad_unlink;
  ilock(ip);

  if(ip->nlink < 1)
    panic("unlink: nlink < 1");
  // 如果是目录，必须为空才能删除
  if(ip->type == T_DIR && !isdirempty(ip)){
    iunlockput(ip);
    goto bad_unlink;
  }

  // 从父目录中删除该目录项（通过将目录项清零）
  memset(&de, 0, sizeof(de));
  if(writei(dp, 0, (uint64)&de, off, sizeof(de)) != sizeof(de))
    panic("unlink: writei");
  if(ip->type == T_DIR){ // 如果删除的是目录，父目录的链接数减一
    dp->nlink--;
    iupdate(dp);
  }
  iunlockput(dp);

  ip->nlink--; // 减少文件的链接计数
  iupdate(ip);
  iunlockput(ip);

  end_op();

  return 0;

bad_unlink:
  iunlockput(dp);
  end_op();
  return -1;
}

// 通用函数：在指定路径创建一个新的 inode（文件、目录或设备）。
//
// @param path: 创建路径。
// @param type: inode 类型 (T_FILE, T_DIR, T_DEVICE)。
// @param major: (设备文件)主设备号。
// @param minor: (设备文件)次设备号。
// @return: 成功则返回锁定的 inode 指针，失败则返回0。
static struct inode*
create(char *path, short type, short major, short minor)
{
  struct inode *ip, *dp;
  char name[DIRSIZ];

  // 查找父目录 inode 和最后一级的文件名
  if((dp = nameiparent(path, name)) == 0)
    return 0;

  ilock(dp);

  // 检查文件是否已存在
  if((ip = dirlookup(dp, name, 0)) != 0){
    iunlockput(dp);
    ilock(ip);
    // 如果是创建文件，并且已存在同名文件或设备，则可以返回现有inode（用于O_CREATE）
    if(type == T_FILE && (ip->type == T_FILE || ip->type == T_DEVICE))
      return ip;
    iunlockput(ip);
    return 0; // 其他情况，例如创建目录但已存在，则失败
  }

  // 分配一个新的 inode
  if((ip = ialloc(dp->dev, type)) == 0){
    iunlockput(dp);
    return 0;
  }

  // 初始化新的 inode
  ilock(ip);
  ip->major = major;
  ip->minor = minor;
  ip->nlink = 1;
  iupdate(ip);

  if(type == T_DIR){  // 如果创建的是目录

    // 在新目录中创建 "." 和 ".." 条目
    // 注意："." 的 nlink 不增加，以避免循环引用计数问题
    if(dirlink(ip, ".", ip->inum) < 0 || dirlink(ip, "..", dp->inum) < 0)
      goto fail_create;
  }

  // 在父目录中创建指向新 inode 的链接
  if(dirlink(dp, name, ip->inum) < 0)
    goto fail_create;

      if(type == T_DIR){
    // 成功创建后，父目录的链接数加一（因为 ".."）
    dp->nlink++;
    iupdate(dp);
  }

  iunlockput(dp);

  return ip; // 返回新创建并锁定的 inode

 fail_create:
  // 创建失败，回滚操作
  // 清理新分配的 inode
  ip->nlink = 0;
  iupdate(ip);
  iunlockput(ip);
  iunlockput(dp);
  return 0;
}

// sys_open 系统调用：打开或创建一个文件。
//
// @return: 成功则返回新的文件描述符，失败则返回-1。
uint64
sys_open(void)
{
  char path[MAXPATH];
  int fd, omode;
  struct file *f;
  struct inode *ip;
  int n;

  // 获取路径和打开模式
  argint(1, &omode);
  if((n = argstr(0, path, MAXPATH)) < 0)
    return -1;

  begin_op();

  if(omode & O_CREATE){ // 如果是创建模式
    ip = create(path, T_FILE, 0, 0);
    if(ip == 0){
      end_op();
      return -1;
    }
  } else { // 如果是打开模式
    if((ip = namei(path)) == 0){ // 查找 inode
      end_op();
      return -1;
    }
    ilock(ip);
    // 目录只能以只读方式打开
    if(ip->type == T_DIR && omode != O_RDONLY){
      iunlockput(ip);
      end_op();
      return -1;
    }
  }

  // 检查设备文件的主设备号是否有效
  if(ip->type == T_DEVICE && (ip->major < 0 || ip->major >= NDEV)){
    iunlockput(ip);
    end_op();
    return -1;
  }

  // 分配文件结构和文件描述符
  if((f = filealloc()) == 0 || (fd = fdalloc(f)) < 0){
    if(f)
      fileclose(f);
    iunlockput(ip);
    end_op();
    return -1;
  }

  // 初始化文件结构
  if(ip->type == T_DEVICE){
    f->type = FD_DEVICE;
    f->major = ip->major;
  } else {
    f->type = FD_INODE;
    f->off = 0;
  }
  f->ip = ip;
  f->readable = !(omode & O_WRONLY);
  f->writable = (omode & O_WRONLY) || (omode & O_RDWR);

  // 如果指定 O_TRUNC，则截断文件（清空文件内容）
  if((omode & O_TRUNC) && ip->type == T_FILE){
    itrunc(ip);
  }

  iunlock(ip);
  end_op();

  return fd;
}

// sys_mkdir 系统调用：创建一个新目录。
//
// @return: 成功返回0，失败返回-1。
uint64
sys_mkdir(void)
{
  char path[MAXPATH];
  struct inode *ip;

  begin_op();
  // 调用 create 函数创建目录类型的 inode
  if(argstr(0, path, MAXPATH) < 0 || (ip = create(path, T_DIR, 0, 0)) == 0){
    end_op();
    return -1;
  }
  iunlockput(ip); // create 返回的是锁定的 inode，使用后需要解锁并释放引用
  end_op();
  return 0;
}

// sys_mknod 系统调用：创建一个设备文件（特殊文件）。
//
// @return: 成功返回0，失败返回-1。
uint64
sys_mknod(void)
{
  struct inode *ip;
  char path[MAXPATH];
  int major, minor;

  begin_op();
  argint(1, &major);
  argint(2, &minor);
  // 调用 create 函数创建设备类型的 inode
  if((argstr(0, path, MAXPATH)) < 0 ||
     (ip = create(path, T_DEVICE, major, minor)) == 0){
    end_op();
    return -1;
  }
  iunlockput(ip);
  end_op();
  return 0;
}

// sys_chdir 系统调用：改变当前进程的工作目录。
//
// @return: 成功返回0，失败返回-1。
uint64
sys_chdir(void)
{
  char path[MAXPATH];
  struct inode *ip;
  struct proc *p = myproc();
  
  begin_op();
  // 查找新目录的 inode
  if(argstr(0, path, MAXPATH) < 0 || (ip = namei(path)) == 0){
    end_op();
    return -1;
  }
  ilock(ip);
  // 必须是一个目录
  if(ip->type != T_DIR){
    iunlockput(ip);
    end_op();
    return -1;
  }
  iunlock(ip);
  iput(p->cwd); // 释放对旧工作目录 inode 的引用
  end_op();
  p->cwd = ip; // 更新当前工作目录
  return 0;
}

// sys_exec 系统调用：加载并执行一个新程序，替换当前进程的内存映像。
//
// @return: 成功时不返回，失败则返回-1。
uint64
sys_exec(void)
{
  char path[MAXPATH], *argv[MAXARG];
  int i;
  uint64 uargv, uarg;

  // 获取可执行文件路径和参数列表的用户空间地址
  argaddr(1, &uargv);
  if(argstr(0, path, MAXPATH) < 0) {
    return -1;
  }
  
  // 将参数列表从用户空间拷贝到内核空间
  memset(argv, 0, sizeof(argv));
  for(i=0;; i++){
    if(i >= NELEM(argv)){ // 参数过多
      goto bad_exec;
    }
    // 从用户空间获取 argv[i] 的指针
    if(fetchaddr(uargv+sizeof(uint64)*i, (uint64*)&uarg) < 0){
      goto bad_exec;
    }
    if(uarg == 0){ // argv 列表以 NULL 指针结束
      argv[i] = 0;
      break;
    }
    argv[i] = kalloc(); // 为参数字符串分配内核内存
    if(argv[i] == 0)
      goto bad_exec;
    // 从用户空间拷贝参数字符串
    if(fetchstr(uarg, argv[i], PGSIZE) < 0)
      goto bad_exec;
  }

  // 调用 exec 执行程序
  int ret = exec(path, argv);

  // 如果 exec 成功，它不会返回。如果返回，说明发生了错误。
  // 释放为参数列表分配的内存。
  for(i = 0; i < NELEM(argv) && argv[i] != 0; i++)
    kfree(argv[i]);

  return ret;

 bad_exec: // exec 失败，清理已分配的内存
  for(i = 0; i < NELEM(argv) && argv[i] != 0; i++)
    kfree(argv[i]);
  return -1;
}

// sys_pipe 系统调用：创建一个管道，用于进程间通信。
//
// @return: 成功返回0，失败返回-1。
uint64
sys_pipe(void)
{
  uint64 fdarray; // 指向用户空间一个包含两个整数的数组的指针
  struct file *rf, *wf;
  int fd0, fd1;
  struct proc *p = myproc();

  argaddr(0, &fdarray);
  // 分配一个管道，得到读端和写端的文件结构
  if(pipealloc(&rf, &wf) < 0)
    return -1;
  fd0 = -1;
  // 为读端和写端分配文件描述符
  if((fd0 = fdalloc(rf)) < 0 || (fd1 = fdalloc(wf)) < 0){
    // 如果分配失败，清理已分配的资源
    if(fd0 >= 0)
      p->ofile[fd0] = 0;
    fileclose(rf);
    fileclose(wf);
    return -1;
  }
  // 将两个文件描述符（读端 fd0 和写端 fd1）拷贝回用户空间
  if(copyout(p->pagetable, fdarray, (char*)&fd0, sizeof(fd0)) < 0 ||
     copyout(p->pagetable, fdarray+sizeof(fd0), (char *)&fd1, sizeof(fd1)) < 0){
    // 如果拷贝失败，清理资源
    p->ofile[fd0] = 0;
    p->ofile[fd1] = 0;
    fileclose(rf);
    fileclose(wf);
    return -1;
  }
  return 0;
}
