#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "proc.h"
#include "defs.h"
#include "elf.h"

static int loadseg(pde_t *, uint64, struct inode *, uint, uint);

// 将 ELF 程序头中的段标志 (flags) 转换为页表项 (PTE) 的权限位。
// ELF flags -> PTE flags
int
flags2perm(int flags)
{
    int perm = 0;
    if(flags & ELF_PROG_FLAG_EXEC) // 如果段是可执行的
      perm = PTE_X;
    if(flags & ELF_PROG_FLAG_WRITE) // 如果段是可写的
      perm |= PTE_W;
    return perm;
}

//
// exec 系统调用：加载并执行一个新程序。
//
// exec(path, argv) 会加载并执行名为 `path` 的可执行文件，
// 并将 `argv` 作为命令行参数传递给它。
// 它会用新程序的内存映像替换当前进程的内存，
// 但会保留相同的文件描述符、进程 ID 和父进程。
// 成功时，它不会返回到调用者进程，而是从新程序的 `main` 函数开始执行。
// 失败时，返回 -1。
//
int
exec(char *path, char **argv)
{
  char *s, *last;
  int i, off;
  uint64 argc, sz = 0, sp, ustack[MAXARG], stackbase;
  struct elfhdr elf;
  struct inode *ip;
  struct proghdr ph;
  pagetable_t pagetable = 0, oldpagetable; // 新旧页表
  struct proc *p = myproc();

  begin_op(); // 开始一个文件系统操作事务，确保操作的原子性

  // 1. 打开路径对应的可执行文件 inode
  if((ip = namei(path)) == 0){
    end_op();
    return -1;
  }
  ilock(ip); // 锁定 inode，防止在读取时文件被修改

  // 2. 读取并验证 ELF 头部
  // 从 inode 的偏移量 0 处读取 ELF 文件头
  if(readi(ip, 0, (uint64)&elf, 0, sizeof(elf)) != sizeof(elf))
    goto bad; // 读取失败或文件大小不足

  // 检查 ELF 魔数 (magic number)，确认这是否是一个有效的 ELF 文件
  if(elf.magic != ELF_MAGIC)
    goto bad;

  // 3. 为新程序创建一个新的用户页表
  // 这个页表将映射新程序的地址空间
  if((pagetable = proc_pagetable(p)) == 0)
    goto bad;

  // 4. 加载 ELF 文件的程序段 (segments) 到内存
  // 遍历 ELF 文件中的所有程序头 (program header)
  for(i=0, off=elf.phoff; i<elf.phnum; i++, off+=sizeof(ph)){
    // 从 inode 中读取一个程序头
    if(readi(ip, 0, (uint64)&ph, off, sizeof(ph)) != sizeof(ph))
      goto bad;
    
    // 我们只关心需要加载到内存的段 (type == ELF_PROG_LOAD)
    if(ph.type != ELF_PROG_LOAD)
      continue;
    
    // 一些健全性检查，确保段的定义是合理的
    if(ph.memsz < ph.filesz) // 段在内存中的大小不能小于其在文件中的大小
      goto bad;
    if(ph.vaddr + ph.memsz < ph.vaddr) // 检查虚拟地址是否溢出
      goto bad;
    if(ph.vaddr % PGSIZE != 0) // 虚拟地址必须是页对齐的
        goto bad;

    // 为当前段分配内存，并设置页表映射
    // uvmalloc 会分配物理内存并将其映射到指定的虚拟地址范围
    uint64 sz1;
    if((sz1 = uvmalloc(pagetable, sz, ph.vaddr + ph.memsz, flags2perm(ph.flags))) == 0)
      goto bad;
    sz = sz1; // 更新当前已分配的内存总大小

    // 将段的内容从文件加载到刚刚分配好的内存中
    if(loadseg(pagetable, ph.vaddr, ip, ph.off, ph.filesz) < 0)
      goto bad;
  }
  iunlockput(ip); // 解锁并释放 inode
  end_op();       // 结束文件系统事务
  ip = 0;         // 将 ip 设为 0，表示 inode 已处理完毕

  p = myproc();
  uint64 oldsz = p->sz; // 保存旧进程的内存大小，以便在最后释放

  // 5. 分配并设置用户栈
  // 首先，将内存大小向上取整到页边界
  sz = PGROUNDUP(sz);
  uint64 sz1;
  // 分配两页：一页作为实际的栈空间，另一页作为保护页 (guard page) 防止栈溢出
  if((sz1 = uvmalloc(pagetable, sz, sz + 2*PGSIZE, PTE_W)) == 0)
    goto bad;
  sz = sz1;
  uvmclear(pagetable, sz - 2*PGSIZE); // 将保护页标记为用户不可访问
  sp = sz; // 栈顶指针 (Stack Pointer) 初始化为新分配区域的顶部
  stackbase = sp - PGSIZE; // 栈底，用于后续检查栈是否向下溢出

  // 6. 将命令行参数 (argv) 压入用户栈
  // 首先，将参数字符串本身复制到栈顶
  for(argc = 0; argv[argc]; argc++) {
    if(argc >= MAXARG) // 检查参数数量是否超过最大限制
      goto bad;
    sp -= strlen(argv[argc]) + 1; // 为字符串和结尾的 '\0' 在栈上分配空间
    sp -= sp % 16; // RISC-V ABI 要求栈指针必须 16 字节对齐
    if(sp < stackbase) // 检查栈是否溢出
      goto bad;
    // 将参数字符串从内核空间复制到用户栈上
    if(copyout(pagetable, sp, argv[argc], strlen(argv[argc]) + 1) < 0)
      goto bad;
    ustack[argc] = sp; // 在内核中暂存用户栈上该参数字符串的地址
  }
  ustack[argc] = 0; // 参数数组以 NULL 指针结尾

  // 接下来，将指向这些字符串的指针数组 (argv) 本身压入栈中
  sp -= (argc+1) * sizeof(uint64);
  sp -= sp % 16; // 同样需要对齐
  if(sp < stackbase)
    goto bad;
  // 将 ustack 中的指针数组从内核复制到用户栈
  if(copyout(pagetable, sp, (char *)ustack, (argc+1)*sizeof(uint64)) < 0)
    goto bad;

  // 7. 准备新程序的初始执行状态
  // user main(argc, argv) 的参数
  // argc 将通过 a0 寄存器传递 (由 syscall wrapper 完成)
  p->trapframe->a1 = sp; // argv 的地址 (指向栈上指针数组的指针) 放入 a1 寄存器

  // 为了调试方便，从路径中提取程序名并保存到进程结构中
  for(last=s=path; *s; s++)
    if(*s == '/')
      last = s+1;
  safestrcpy(p->name, last, sizeof(p->name));
    
  // 8. 提交新的内存映像，正式替换旧的进程映像
  oldpagetable = p->pagetable; // 保存旧页表的地址
  p->pagetable = pagetable;     // 切换到新页表
  p->sz = sz;                   // 更新进程大小
  p->trapframe->epc = elf.entry;  // 设置程序计数器 (EPC) 为 ELF 文件的入口点
  p->trapframe->sp = sp;          // 设置新的栈指针 (SP)
  proc_freepagetable(oldpagetable, oldsz); // 释放旧的页表和其映射的所有物理内存

  // exec 成功。返回的 argc 会被 syscall handling code 放入 a0 寄存器。
  // 当从内核返回到用户空间时，这个值会成为 `main` 函数的第一个参数。
  return argc; 

 bad: // 错误处理流程
  if(pagetable)
    proc_freepagetable(pagetable, sz); // 如果创建了新页表，则释放它
  if(ip){
    iunlockput(ip); // 如果锁定了 inode，则解锁并释放它
    end_op();
  }
  return -1; // 返回错误码
}

//
// loadseg: 将一个程序段加载到指定的虚拟地址
//
// 从 inode 的 `offset` 处读取 `sz` 字节的数据，
// 并将其加载到 `pagetable` 中虚拟地址为 `va` 的位置。
// `va` 必须是页对齐的，并且 `va` 到 `va+sz` 的虚拟地址空间
// 必须已经通过 uvmalloc 分配好了。
//
// 成功返回 0，失败返回 -1。
//
static int
loadseg(pagetable_t pagetable, uint64 va, struct inode *ip, uint offset, uint sz)
{
  uint i, n;
  uint64 pa;

  if((va % PGSIZE) != 0)
    panic("loadseg: va must be page aligned");

  // 遍历该段所占用的所有页
  for(i = 0; i < sz; i += PGSIZE){
    // 找到当前虚拟地址 va+i 对应的物理地址 pa
    // uvmalloc 应该已经为这些地址创建了映射，所以 walkaddr 不应该失败
    pa = walkaddr(pagetable, va + i);
    if(pa == 0)
      panic("loadseg: address should exist");

    // 计算本次循环需要从文件中读取多少字节
    if(sz - i < PGSIZE)
      n = sz - i; // 如果是最后一个页，可能不满一页
    else
      n = PGSIZE; // 否则读取整页
    
    // 从 inode 的指定偏移量读取数据，直接写入到物理地址 pa
    if(readi(ip, 0, (uint64)pa, offset+i, n) != n)
      return -1;
  }
  
  return 0;
}
