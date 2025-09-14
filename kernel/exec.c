#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "proc.h"
#include "defs.h"
#include "elf.h"

static int loadseg(pde_t *, uint64, struct inode *, uint, uint);

// 将ELF权限映射到PTE权限位。
int flags2perm(int flags)
{
  int perm = 0;    // 初始化权限为0
  if (flags & 0x1) // 如果有执行权限
    perm = PTE_X;  // 设置PTE_X位
  if (flags & 0x2) // 如果有写权限
    perm |= PTE_W; // 设置PTE_W位
  return perm;     // 返回计算出的权限
}

//
// exec() 系统调用的实现
//
int kexec(char *path, char **argv)
{
  char *s, *last;                                     // 临时字符串指针
  int i, off;                                         // 循环变量和偏移量
  uint64 argc, sz = 0, sp, ustack[MAXARG], stackbase; // 参数计数，大小，栈指针，用户栈，栈底
  struct elfhdr elf;                                  // ELF头结构
  struct inode *ip;                                   // inode指针
  struct proghdr ph;                                  // 程序头结构
  pagetable_t pagetable = 0, oldpagetable;            // 页表指针
  struct proc *p = myproc();                          // 获取当前进程

  begin_op(); // 开始文件系统操作

  // 打开可执行文件。
  if ((ip = namei(path)) == 0)
  {            // 通过路径名查找inode
    end_op();  // 结束文件系统操作
    return -1; // 查找失败，返回-1
  }
  ilock(ip); // 锁定inode

  // 读取ELF头。
  if (readi(ip, 0, (uint64)&elf, 0, sizeof(elf)) != sizeof(elf)) // 读取ELF头
    goto bad;                                                    // 读取失败，跳转到bad

  // 这真的是一个ELF文件吗？
  if (elf.magic != ELF_MAGIC) // 检查ELF魔数
    goto bad;                 // 不是ELF文件，跳转到bad

  if ((pagetable = proc_pagetable(p)) == 0) // 创建新的页表
    goto bad;                               // 创建失败，跳转到bad

  // 将程序加载到内存中。
  for (i = 0, off = elf.phoff; i < elf.phnum; i++, off += sizeof(ph))
  {                                                               // 遍历程序头表
    if (readi(ip, 0, (uint64)&ph, off, sizeof(ph)) != sizeof(ph)) // 读取程序头
      goto bad;                                                   // 读取失败，跳转到bad
    if (ph.type != ELF_PROG_LOAD)                                 // 如果不是可加载段
      continue;                                                   // 继续下一个
    if (ph.memsz < ph.filesz)                                     // 内存大小不能小于文件大小
      goto bad;                                                   // 检查失败，跳转到bad
    if (ph.vaddr + ph.memsz < ph.vaddr)                           // 检查地址溢出
      goto bad;                                                   // 检查失败，跳转到bad
    if (ph.vaddr % PGSIZE != 0)                                   // 虚拟地址必须页对齐
      goto bad;                                                   // 检查失败，跳转到bad
    uint64 sz1;
    if ((sz1 = uvmalloc(pagetable, sz, ph.vaddr + ph.memsz, flags2perm(ph.flags))) == 0) // 分配内存
      goto bad;                                                                          // 分配失败，跳转到bad
    sz = sz1;                                                                            // 更新大小
    if (loadseg(pagetable, ph.vaddr, ip, ph.off, ph.filesz) < 0)                         // 加载段到内存
      goto bad;                                                                          // 加载失败，跳转到bad
  }
  iunlockput(ip); // 解锁并释放inode
  end_op();       // 结束文件系统操作
  ip = 0;         // inode指针置空

  p = myproc();         // 再次获取当前进程
  uint64 oldsz = p->sz; // 保存旧的进程大小

  // 在下一个页面边界分配一些页面。
  // 使第一个页面不可访问作为堆栈保护。
  // 将其余部分用作用户堆栈。
  sz = PGROUNDUP(sz); // 将大小向上取整到页边界
  uint64 sz1;
  if ((sz1 = uvmalloc(pagetable, sz, sz + (USERSTACK + 1) * PGSIZE, PTE_W)) == 0) // 为用户栈分配内存
    goto bad;                                                                     // 分配失败，跳转到bad
  sz = sz1;                                                                       // 更新大小
  uvmclear(pagetable, sz - (USERSTACK + 1) * PGSIZE);                             // 清除保护页的PTE_U位
  sp = sz;                                                                        // 栈指针指向栈顶
  stackbase = sp - USERSTACK * PGSIZE;                                            // 计算栈底

  // 将参数字符串复制到新堆栈中，记住它们在ustack[]中的地址。
  for (argc = 0; argv[argc]; argc++)
  {                                                                     // 遍历参数
    if (argc >= MAXARG)                                                 // 参数数量超过最大值
      goto bad;                                                         // 跳转到bad
    sp -= strlen(argv[argc]) + 1;                                       // 为参数字符串分配空间
    sp -= sp % 16;                                                      // riscv sp必须是16字节对齐的
    if (sp < stackbase)                                                 // 栈溢出检查
      goto bad;                                                         // 跳转到bad
    if (copyout(pagetable, sp, argv[argc], strlen(argv[argc]) + 1) < 0) // 拷贝参数字符串到用户栈
      goto bad;                                                         // 拷贝失败，跳转到bad
    ustack[argc] = sp;                                                  // 保存参数字符串的地址
  }
  ustack[argc] = 0; // 参数列表以空指针结尾

  // 推入ustack[]的副本，即argv[]指针的数组。
  sp -= (argc + 1) * sizeof(uint64);                                           // 为argv数组分配空间
  sp -= sp % 16;                                                               // 16字节对齐
  if (sp < stackbase)                                                          // 栈溢出检查
    goto bad;                                                                  // 跳转到bad
  if (copyout(pagetable, sp, (char *)ustack, (argc + 1) * sizeof(uint64)) < 0) // 拷贝argv数组到用户栈
    goto bad;                                                                  // 拷贝失败，跳转到bad

  // a0和a1包含用户main(argc, argv)的参数
  // argc通过系统调用返回值返回，该值位于a0中。
  p->trapframe->a1 = sp; // a1寄存器保存argv的地址

  // 保存程序名称以进行调试。
  for (last = s = path; *s; s++)              // 遍历路径字符串
    if (*s == '/')                            // 找到最后一个'/'
      last = s + 1;                           // last指向程序名
  safestrcpy(p->name, last, sizeof(p->name)); // 拷贝程序名到进程结构中

  // 提交到用户映像。
  oldpagetable = p->pagetable;             // 保存旧的页表
  p->pagetable = pagetable;                // 切换到新的页表
  p->sz = sz;                              // 更新进程大小
  p->trapframe->epc = elf.entry;           // 初始程序计数器 = main
  p->trapframe->sp = sp;                   // 初始堆栈指针
  proc_freepagetable(oldpagetable, oldsz); // 释放旧的页表和内存

  return argc; // 这最终会进入a0，即main(argc, argv)的第一个参数

bad:
  if (pagetable)                       // 如果创建了新的页表
    proc_freepagetable(pagetable, sz); // 释放新的页表和内存
  if (ip)
  {                 // 如果打开了inode
    iunlockput(ip); // 解锁并释放inode
    end_op();       // 结束文件系统操作
  }
  return -1; // 返回错误
}

// 将ELF程序段加载到页表中的虚拟地址va。
// va必须是页面对齐的
// 并且从va到va+sz的页面必须已经映射。
// 成功返回0，失败返回-1。
static int
loadseg(pagetable_t pagetable, uint64 va, struct inode *ip, uint offset, uint sz)
{
  uint i, n; // 循环变量和大小
  uint64 pa; // 物理地址

  for (i = 0; i < sz; i += PGSIZE)
  {                                           // 按页加载
    pa = walkaddr(pagetable, va + i);         // 获取虚拟地址对应的物理地址
    if (pa == 0)                              // 物理地址为空
      panic("loadseg: address should exist"); // 恐慌
    if (sz - i < PGSIZE)                      // 如果剩余大小小于一页
      n = sz - i;                             // n为剩余大小
    else
      n = PGSIZE;                                     // n为一页大小
    if (readi(ip, 0, (uint64)pa, offset + i, n) != n) // 从inode读取数据到物理地址
      return -1;                                      // 读取失败，返回-1
  }

  return 0; // 成功返回0
}
