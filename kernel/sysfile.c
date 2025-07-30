//
// 文件系统相关的系统调用。
// 主要进行参数检查（因为我们不信任用户代码），
// 然后调用 file.c 和 fs.c 中的函数来完成实际工作。
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

// 获取第 n 个字大小的系统调用参数作为文件描述符，
// 并返回该描述符和对应的 struct file 指针。
static int
argfd(int n, int *pfd, struct file **pf)
{
  int fd;
  struct file *f;

  argint(n, &fd);
  if(fd < 0 || fd >= NOFILE || (f=myproc()->ofile[fd]) == 0)
    return -1;
  if(pfd)
    *pfd = fd;
  if(pf)
    *pf = f;
  return 0;
}

// 为给定的文件分配一个文件描述符。
// 成功后，调用者对文件的引用被接管。
static int
fdalloc(struct file *f)
{
  int fd;
  struct proc *p = myproc();

  for(fd = 0; fd < NOFILE; fd++){
    if(p->ofile[fd] == 0){
      p->ofile[fd] = f;
      return fd;
    }
  }
  return -1;
}

// sys_dup 系统调用：复制一个文件描述符。
uint64
sys_dup(void)
{
  struct file *f;
  int fd;

  if(argfd(0, 0, &f) < 0) // 获取第0个参数（旧的文件描述符）
    return -1;
  if((fd=fdalloc(f)) < 0) // 分配一个新的文件描述符
    return -1;
  filedup(f); // 增加文件的引用计数
  return fd;
}

// sys_read 系统调用：从文件读取数据。
uint64
sys_read(void)
{
  struct file *f;
  int n;
  uint64 p;

  argaddr(1, &p); // 获取第1个参数（用户缓冲区地址）
  argint(2, &n);  // 获取第2个参数（要读取的字节数）
  if(argfd(0, 0, &f) < 0) // 获取第0个参数（文件描述符）
    return -1;
  return fileread(f, p, n);
}

// sys_write 系统调用：向文件写入数据。
uint64
sys_write(void)
{
  struct file *f;
  int n;
  uint64 p;
  
  argaddr(1, &p); // 获取第1个参数（用户缓冲区地址）
  argint(2, &n);  // 获取第2个参数（要写入的字节数）
  if(argfd(0, 0, &f) < 0) // 获取第0个参数（文件描述符）
    return -1;

  return filewrite(f, p, n);
}

// sys_close 系统调用：关闭一个文件描述符。
uint64
sys_close(void)
{
  int fd;
  struct file *f;

  if(argfd(0, &fd, &f) < 0)
    return -1;
  myproc()->ofile[fd] = 0; // 从进程的文件表中移除
  fileclose(f); // 关闭文件（减少引用计数，如果为0则释放资源）
  return 0;
}

// sys_fstat 系统调用：获取文件状态。
uint64
sys_fstat(void)
{
  struct file *f;
  uint64 st; // 指向 struct stat 的用户指针

  argaddr(1, &st);
  if(argfd(0, 0, &f) < 0)
    return -1;
  return filestat(f, st);
}

// sys_link 系统调用：为 old 创建一个名为 new 的硬链接。
uint64
sys_link(void)
{
  char name[DIRSIZ], new[MAXPATH], old[MAXPATH];
  struct inode *dp, *ip;

  if(argstr(0, old, MAXPATH) < 0 || argstr(1, new, MAXPATH) < 0)
    return -1;

  begin_op();
  if((ip = namei(old)) == 0){ // 查找旧路径的 inode
    end_op();
    return -1;
  }

  ilock(ip);
  if(ip->type == T_DIR){ // 不能对目录创建硬链接
    iunlockput(ip);
    end_op();
    return -1;
  }

  ip->nlink++; // 增加链接计数
  iupdate(ip); // 更新 inode 到磁盘
  iunlock(ip);

  if((dp = nameiparent(new, name)) == 0) // 查找新路径的父目录
    goto bad;
  ilock(dp);
  if(dp->dev != ip->dev || dirlink(dp, name, ip->inum) < 0){ // 在父目录中创建链接
    iunlockput(dp);
    goto bad;
  }
  iunlockput(dp);
  iput(ip);

  end_op();

  return 0;

bad: // 链接失败，回滚操作
  ilock(ip);
  ip->nlink--;
  iupdate(ip);
  iunlockput(ip);
  end_op();
  return -1;
}

// 目录 dp 是否为空（除了 "." 和 ".."）？
static int
isdirempty(struct inode *dp)
{
  int off;
  struct dirent de;

  // 遍历目录项，跳过 "." 和 ".."
  for(off=2*sizeof(de); off<dp->size; off+=sizeof(de)){
    if(readi(dp, 0, (uint64)&de, off, sizeof(de)) != sizeof(de))
      panic("isdirempty: readi");
    if(de.inum != 0) // 如果找到一个有效的目录项
      return 0;
  }
  return 1;
}

// sys_unlink 系统调用：删除一个文件链接。
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

  // 不能删除 "." 或 ".."
  if(namecmp(name, ".") == 0 || namecmp(name, "..") == 0)
    goto bad_unlink;

  if((ip = dirlookup(dp, name, &off)) == 0) // 在父目录中查找文件
    goto bad_unlink;
  ilock(ip);

  if(ip->nlink < 1)
    panic("unlink: nlink < 1");
  if(ip->type == T_DIR && !isdirempty(ip)){ // 如果是目录，必须为空
    iunlockput(ip);
    goto bad_unlink;
  }

  memset(&de, 0, sizeof(de)); // 清空目录项
  if(writei(dp, 0, (uint64)&de, off, sizeof(de)) != sizeof(de))
    panic("unlink: writei");
  if(ip->type == T_DIR){ // 如果删除的是目录，父目录的链接数减一
    dp->nlink--;
    iupdate(dp);
  }
  iunlockput(dp);

  ip->nlink--; // 文件的链接数减一
  iupdate(ip);
  iunlockput(ip);

  end_op();

  return 0;

bad_unlink:
  iunlockput(dp);
  end_op();
  return -1;
}

// 创建一个 inode 的通用函数
static struct inode*
create(char *path, short type, short major, short minor)
{
  struct inode *ip, *dp;
  char name[DIRSIZ];

  if((dp = nameiparent(path, name)) == 0) // 查找父目录
    return 0;

  ilock(dp);

  if((ip = dirlookup(dp, name, 0)) != 0){ // 检查文件是否已存在
    iunlockput(dp);
    ilock(ip);
    // 如果是创建文件，且已存在同名文件或设备，则可覆盖
    if(type == T_FILE && (ip->type == T_FILE || ip->type == T_DEVICE))
      return ip;
    iunlockput(ip);
    return 0;
  }

  if((ip = ialloc(dp->dev, type)) == 0){ // 分配一个新的 inode
    iunlockput(dp);
    return 0;
  }

  ilock(ip);
  ip->major = major;
  ip->minor = minor;
  ip->nlink = 1;
  iupdate(ip);

  if(type == T_DIR){  // 如果是目录，创建 "." 和 ".." 条目
    // "." 的 nlink 不增加，避免循环引用计数
    if(dirlink(ip, ".", ip->inum) < 0 || dirlink(ip, "..", dp->inum) < 0)
      goto fail_create;
  }

  if(dirlink(dp, name, ip->inum) < 0) // 在父目录中链接新 inode
    goto fail_create;

  if(type == T_DIR){
    // 成功创建后，父目录的链接数加一（因为 ".."）
    dp->nlink++;
    iupdate(dp);
  }

  iunlockput(dp);

  return ip;

 fail_create:
  // 出错了，释放已分配的 ip
  ip->nlink = 0;
  iupdate(ip);
  iunlockput(ip);
  iunlockput(dp);
  return 0;
}

// sys_open 系统调用：打开或创建一个文件。
uint64
sys_open(void)
{
  char path[MAXPATH];
  int fd, omode;
  struct file *f;
  struct inode *ip;
  int n;

  argint(1, &omode);
  if((n = argstr(0, path, MAXPATH)) < 0)
    return -1;

  begin_op();

  if(omode & O_CREATE){
    ip = create(path, T_FILE, 0, 0);
    if(ip == 0){
      end_op();
      return -1;
    }
  } else {
    if((ip = namei(path)) == 0){
      end_op();
      return -1;
    }
    ilock(ip);
    if(ip->type == T_DIR && omode != O_RDONLY){ // 目录只能以只读方式打开
      iunlockput(ip);
      end_op();
      return -1;
    }
  }

  if(ip->type == T_DEVICE && (ip->major < 0 || ip->major >= NDEV)){
    iunlockput(ip);
    end_op();
    return -1;
  }

  if((f = filealloc()) == 0 || (fd = fdalloc(f)) < 0){
    if(f)
      fileclose(f);
    iunlockput(ip);
    end_op();
    return -1;
  }

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

  if((omode & O_TRUNC) && ip->type == T_FILE){ // 如果指定，截断文件
    itrunc(ip);
  }

  iunlock(ip);
  end_op();

  return fd;
}

// sys_mkdir 系统调用：创建一个新目录。
uint64
sys_mkdir(void)
{
  char path[MAXPATH];
  struct inode *ip;

  begin_op();
  if(argstr(0, path, MAXPATH) < 0 || (ip = create(path, T_DIR, 0, 0)) == 0){
    end_op();
    return -1;
  }
  iunlockput(ip);
  end_op();
  return 0;
}

// sys_mknod 系统调用：创建一个设备文件。
uint64
sys_mknod(void)
{
  struct inode *ip;
  char path[MAXPATH];
  int major, minor;

  begin_op();
  argint(1, &major);
  argint(2, &minor);
  if((argstr(0, path, MAXPATH)) < 0 ||
     (ip = create(path, T_DEVICE, major, minor)) == 0){
    end_op();
    return -1;
  }
  iunlockput(ip);
  end_op();
  return 0;
}

// sys_chdir 系统调用：改变当前工作目录。
uint64
sys_chdir(void)
{
  char path[MAXPATH];
  struct inode *ip;
  struct proc *p = myproc();
  
  begin_op();
  if(argstr(0, path, MAXPATH) < 0 || (ip = namei(path)) == 0){
    end_op();
    return -1;
  }
  ilock(ip);
  if(ip->type != T_DIR){
    iunlockput(ip);
    end_op();
    return -1;
  }
  iunlock(ip);
  iput(p->cwd); // 释放旧的当前目录 inode
  end_op();
  p->cwd = ip; // 设置新的当前目录
  return 0;
}

// sys_exec 系统调用：执行一个新程序。
uint64
sys_exec(void)
{
  char path[MAXPATH], *argv[MAXARG];
  int i;
  uint64 uargv, uarg;

  argaddr(1, &uargv);
  if(argstr(0, path, MAXPATH) < 0) {
    return -1;
  }
  
  memset(argv, 0, sizeof(argv));
  for(i=0;; i++){
    if(i >= NELEM(argv)){ // 参数过多
      goto bad_exec;
    }
    // 从用户空间获取 argv[i] 的指针
    if(fetchaddr(uargv+sizeof(uint64)*i, (uint64*)&uarg) < 0){
      goto bad_exec;
    }
    if(uarg == 0){ // argv 列表结束
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

  int ret = exec(path, argv);

  // exec 成功时不会返回，如果返回了，说明出错了。
  // 释放为参数分配的内存。
  for(i = 0; i < NELEM(argv) && argv[i] != 0; i++)
    kfree(argv[i]);

  return ret;

 bad_exec:
  for(i = 0; i < NELEM(argv) && argv[i] != 0; i++)
    kfree(argv[i]);
  return -1;
}

// sys_pipe 系统调用：创建一个管道。
uint64
sys_pipe(void)
{
  uint64 fdarray; // 指向一个包含两个整数的数组的用户指针
  struct file *rf, *wf;
  int fd0, fd1;
  struct proc *p = myproc();

  argaddr(0, &fdarray);
  if(pipealloc(&rf, &wf) < 0)
    return -1;
  fd0 = -1;
  if((fd0 = fdalloc(rf)) < 0 || (fd1 = fdalloc(wf)) < 0){
    if(fd0 >= 0)
      p->ofile[fd0] = 0;
    fileclose(rf);
    fileclose(wf);
    return -1;
  }
  // 将两个文件描述符写回用户空间
  if(copyout(p->pagetable, fdarray, (char*)&fd0, sizeof(fd0)) < 0 ||
     copyout(p->pagetable, fdarray+sizeof(fd0), (char *)&fd1, sizeof(fd1)) < 0){
    p->ofile[fd0] = 0;
    p->ofile[fd1] = 0;
    fileclose(rf);
    fileclose(wf);
    return -1;
  }
  return 0;
}
