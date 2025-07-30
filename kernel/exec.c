#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "proc.h"
#include "defs.h"
#include "elf.h"

static int loadseg(pde_t *, uint64, struct inode *, uint, uint);

// 将 ELF 程序头中的标志转换为页表权限位。
int flags2perm(int flags)
{
    int perm = 0;
    if(flags & 0x1) // 可执行标志
      perm = PTE_X;
    if(flags & 0x2) // 可写标志
      perm |= PTE_W;
    return perm;
}

int
exec(char *path, char **argv)
{
  char *s, *last;
  int i, off;
  uint64 argc, sz = 0, sp, ustack[MAXARG], stackbase;
  struct elfhdr elf;
  struct inode *ip;
  struct proghdr ph;
  pagetable_t pagetable = 0, oldpagetable;
  struct proc *p = myproc();

  begin_op(); // 开始一个文件系统操作块

  // 通过路径名查找可执行文件的 inode
  if((ip = namei(path)) == 0){
    end_op();
    return -1;
  }
  ilock(ip); // 锁定 inode

  // 检查 ELF 头部
  if(readi(ip, 0, (uint64)&elf, 0, sizeof(elf)) != sizeof(elf))
    goto bad;

  // 检查文件是否为 ELF 文件
  if(elf.magic != ELF_MAGIC)
    goto bad;

  // 为新进程创建一个新的空页表
  if((pagetable = proc_pagetable(p)) == 0)
    goto bad;

  // 将程序加载到内存中。
  for(i=0, off=elf.phoff; i<elf.phnum; i++, off+=sizeof(ph)){
    // 读取程序段头
    if(readi(ip, 0, (uint64)&ph, off, sizeof(ph)) != sizeof(ph))
      goto bad;
    if(ph.type != ELF_PROG_LOAD) // 只加载类型为 LOAD 的段
      continue;
    if(ph.memsz < ph.filesz) // 内存中的大小不能小于文件中的大小
      goto bad;
    if(ph.vaddr + ph.memsz < ph.vaddr) // 检查地址溢出
      goto bad;
    if(ph.vaddr % PGSIZE != 0) // 虚拟地址必须是页对齐的
      goto bad;
    uint64 sz1;
    // 为段分配内存
    if((sz1 = uvmalloc(pagetable, sz, ph.vaddr + ph.memsz, flags2perm(ph.flags))) == 0)
      goto bad;
    sz = sz1;
    // 将段加载到分配的内存中
    if(loadseg(pagetable, ph.vaddr, ip, ph.off, ph.filesz) < 0)
      goto bad;
  }
  iunlockput(ip); // 解锁并释放 inode
  end_op(); // 结束文件系统操作
  ip = 0;

  p = myproc();
  uint64 oldsz = p->sz; // 保存旧的进程大小

  // 分配用户栈。
  // 在当前大小之上分配两个页，一个用作栈，一个用作保护页。
  sz = PGROUNDUP(sz);
  uint64 sz1;
  if((sz1 = uvmalloc(pagetable, sz, sz + (USERSTACK+1)*PGSIZE, PTE_W)) == 0)
    goto bad;
  sz = sz1;
  // 清除保护页的 PTE_U 位，使其在用户态不可访问。
  uvmclear(pagetable, sz-(USERSTACK+1)*PGSIZE);
  sp = sz; // 初始栈顶指针
  stackbase = sp - USERSTACK*PGSIZE; // 栈底地址

  // 将参数字符串压入栈，并在 ustack 数组中准备指针。
  for(argc = 0; argv[argc]; argc++) {
    if(argc >= MAXARG) // 检查参数数量是否超过最大值
      goto bad;
    sp -= strlen(argv[argc]) + 1;
    sp -= sp % 16; // RISC-V 要求栈指针是 16 字节对齐的
    if(sp < stackbase) // 检查栈是否溢出
      goto bad;
    // 将参数字符串复制到栈上
    if(copyout(pagetable, sp, argv[argc], strlen(argv[argc]) + 1) < 0)
      goto bad;
    ustack[argc] = sp;
  }
  ustack[argc] = 0; // 参数数组以 NULL 结尾

  // 将 argv 指针数组压入栈中。
  sp -= (argc+1) * sizeof(uint64);
  sp -= sp % 16;
  if(sp < stackbase)
    goto bad;
  if(copyout(pagetable, sp, (char *)ustack, (argc+1)*sizeof(uint64)) < 0)
    goto bad;

  // 设置用户 main 函数的参数 (argc, argv)
  // argc 通过系统调用返回值传递 (在 a0 寄存器中)。
  p->trapframe->a1 = sp; // argv 的地址放在 a1 寄存器中

  // 为调试保存程序名。
  for(last=s=path; *s; s++)
    if(*s == '/')
      last = s+1;
  safestrcpy(p->name, last, sizeof(p->name));
    
  // 提交新的用户镜像。
  oldpagetable = p->pagetable;
  p->pagetable = pagetable;
  p->sz = sz;
  p->trapframe->epc = elf.entry;  // 设置程序计数器为 ELF 文件的入口点
  p->trapframe->sp = sp; // 设置新的栈指针
  proc_freepagetable(oldpagetable, oldsz); // 释放旧的页表和内存

  return argc; // 返回值 argc 会被放入 a0，成为 main 的第一个参数

 bad: // 错误处理流程
  if(pagetable)
    proc_freepagetable(pagetable, sz);
  if(ip){
    iunlockput(ip);
    end_op();
  }
  return -1;
}

// 将一个程序段加载到页表中指定的虚拟地址 va。
// va 必须是页对齐的，
// 并且从 va 到 va+sz 的页面必须已经被映射。
// 成功返回 0，失败返回 -1。
static int
loadseg(pagetable_t pagetable, uint64 va, struct inode *ip, uint offset, uint sz)
{
  uint i, n;
  uint64 pa;

  for(i = 0; i < sz; i += PGSIZE){
    // 查找虚拟地址对应的物理地址
    pa = walkaddr(pagetable, va + i);
    if(pa == 0)
      panic("loadseg: address should exist"); // 地址应该已经存在
    // 计算这次要读取多少字节
    if(sz - i < PGSIZE)
      n = sz - i;
    else
      n = PGSIZE;
    // 从 inode 的指定偏移量读取数据到物理地址
    if(readi(ip, 0, (uint64)pa, offset+i, n) != n)
      return -1;
  }
  
  return 0;
}
