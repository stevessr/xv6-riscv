#include "param.h"
#include "types.h"
#include "memlayout.h"
#include "elf.h"
#include "riscv.h"
#include "defs.h"
#include "fs.h"

/*
 * xv6 的虚拟内存管理。
 *
 * RISC-V 使用三级页表结构 (Sv39)。
 * 一个 64 位的虚拟地址被划分为以下几个部分：
 * 39..63 -- 必须为0 (或者等于第38位，取决于实现)
 * 30..38 -- 9 位 L2 页表索引
 * 21..29 -- 9 位 L1 页表索引
 * 12..20 -- 9 位 L0 页表索引
 *  0..11 -- 12 位页内偏移
 *
 * 一个页表页 (page-table page) 包含 512 个 64 位的页表项 (PTE)。
 * 每个 PTE 包含一个 44 位的物理页号 (PPN) 和一些标志位。
 */

/*
 * 内核的页表。
 */
pagetable_t kernel_pagetable;

extern char etext[];  // kernel.ld 将此符号设置为内核代码段的末尾。

extern char trampoline[]; // trampoline.S 中定义的跳板代码地址。

// 为内核创建一个直接映射的页表。
pagetable_t
kvmmake(void)
{
  pagetable_t kpgtbl;

  // 分配一个物理页作为 L2 页表。
  kpgtbl = (pagetable_t) kalloc();
  memset(kpgtbl, 0, PGSIZE);

  // 映射 UART 寄存器。
  kvmmap(kpgtbl, UART0, UART0, PGSIZE, PTE_R | PTE_W);

  // 映射 virtio mmio 磁盘接口。
  kvmmap(kpgtbl, VIRTIO0, VIRTIO0, PGSIZE, PTE_R | PTE_W);

  // 映射 PLIC (Platform-Level Interrupt Controller)。
  kvmmap(kpgtbl, PLIC, PLIC, 0x4000000, PTE_R | PTE_W);

  // 映射内核文本段 (代码)，权限为可读、可执行。
  kvmmap(kpgtbl, KERNBASE, KERNBASE, (uint64)etext-KERNBASE, PTE_R | PTE_X);

  // 映射内核数据段和剩余的所有物理内存，权限为可读、可写。
  kvmmap(kpgtbl, (uint64)etext, (uint64)etext, PHYSTOP-(uint64)etext, PTE_R | PTE_W);
  
  // 映射 QEMU 关机设备地址 (0x100000)
  kvmmap(kpgtbl, 0x100000, 0x100000, PGSIZE, PTE_R | PTE_W);

  // 将跳板页映射到内核虚拟地址空间的最高处。
  // 这个页面同时也会被映射到每个用户进程的虚拟地址空间中。
  kvmmap(kpgtbl, TRAMPOLINE, (uint64)trampoline, PGSIZE, PTE_R | PTE_X);

  // 为每个进程分配并映射一个内核栈。
  proc_mapstacks(kpgtbl);
  
  return kpgtbl;
}

// 初始化唯一的内核页表
void
kvminit(void)
{
  kernel_pagetable = kvmmake();
}

// 将硬件页表寄存器 (satp) 切换到内核页表，并启用分页。
void
kvminithart()
{
  // 等待对页表内存的任何先前写入完成。
  sfence_vma();

  // 将 satp 寄存器设置为内核页表的物理地址，并指定 Sv39 分页模式。
  w_satp(MAKE_SATP(kernel_pagetable));

  // 刷新 TLB (Translation Lookaside Buffer) 中的旧条目。
  sfence_vma();
}

// 在页表 pagetable 中，返回虚拟地址 va 对应的
// PTE（页表条目）的地址。如果 alloc!=0，则创建所有
// 必需的页表页。
//
// RISC-V Sv39 寻址方案包含三级页表。
// 一个页表页包含 512 个 64 位的 PTE。
// 一个 64 位的虚拟地址被分为五个字段：
//   63..39 -- 必须为零。
//   38..30 -- 9 位的 2 级页表索引。
//   29..21 -- 9 位的 1 级页表索引。
//   20..12 -- 9 位的 0 级页表索引。
//   11..0  -- 12 位的页内字节偏移。
pte_t *
walk(pagetable_t pagetable, uint64 va, int alloc)
{
  if(va >= MAXVA)
    panic("walk");

  // 从 L2 级别开始向下遍历三级页表
  for(int level = 2; level > 0; level--) {
    pte_t *pte = &pagetable[PX(level, va)];
    if(*pte & PTE_V) {
      // PTE 有效，获取下一级页表的物理地址
      pagetable = (pagetable_t)PTE2PA(*pte);
    } else {
      // PTE 无效，需要分配一个新的页表页
      if(!alloc || (pagetable = (pde_t*)kalloc()) == 0)
        return 0; // 分配失败
      memset(pagetable, 0, PGSIZE);
      // 将新分配的页表页的物理地址填入 PTE，并设置有效位
      *pte = PA2PTE(pagetable) | PTE_V;
    }
  }
  // 返回最终 L0 页表中对应 va 的 PTE 地址
  return &pagetable[PX(0, va)];
}

// 查找一个虚拟地址，返回其物理地址。
// 如果没有映射，则返回 0。
// 只能用于查找用户页。
uint64
walkaddr(pagetable_t pagetable, uint64 va)
{
  pte_t *pte;
  uint64 pa;

  if(va >= MAXVA)
    return 0;

  pte = walk(pagetable, va, 0);
  if(pte == 0) // walk 失败
    return 0;
  if((*pte & PTE_V) == 0) // 页面不存在
    return 0;
  if((*pte & PTE_U) == 0) // 页面不是用户页
    return 0;
  
  pa = PTE2PA(*pte);
  return pa;
}

// 向内核页表添加一个映射。
// 仅在启动时使用。
// 不会刷新 TLB 或启用分页。
void
kvmmap(pagetable_t kpgtbl, uint64 va, uint64 pa, uint64 sz, int perm)
{
  if(mappages(kpgtbl, va, sz, pa, perm) != 0)
    panic("kvmmap");
}

// 为从 va 开始的虚拟地址创建 PTE，这些 PTE 指向从 pa 开始的物理地址。
// va 和 size 必须是页对齐的。
// 成功返回 0，如果 walk() 无法分配所需的页表页，则返回 -1。
int
mappages(pagetable_t pagetable, uint64 va, uint64 size, uint64 pa, int perm)
{
  uint64 a, last;
  pte_t *pte;

  if((va % PGSIZE) != 0)
    panic("mappages: va not aligned");

  if((size % PGSIZE) != 0)
    panic("mappages: size not aligned");

  if(size == 0)
    panic("mappages: size");
  
  a = va;
  last = va + size - PGSIZE;
  for(;;){
    if((pte = walk(pagetable, a, 1)) == 0)
      return -1;
    if(*pte & PTE_V) // PTE 已被映射
      panic("mappages: remap");
    *pte = PA2PTE(pa) | perm | PTE_V;
    if(a == last)
      break;
    a += PGSIZE;
    pa += PGSIZE;
  }
  return 0;
}

// 从 va 开始，移除 npages 个页面的映射。va 必须是页对齐的。
// 映射必须存在。
// 可选择性地释放物理内存。
void
uvmunmap(pagetable_t pagetable, uint64 va, uint64 npages, int do_free)
{
  uint64 a;
  pte_t *pte;

  if((va % PGSIZE) != 0)
    panic("uvmunmap: not aligned");

  for(a = va; a < va + npages*PGSIZE; a += PGSIZE){
    if((pte = walk(pagetable, a, 0)) == 0)
      panic("uvmunmap: walk");
    if((*pte & PTE_V) == 0)
      panic("uvmunmap: not mapped");
    if(PTE_FLAGS(*pte) == PTE_V)
      panic("uvmunmap: not a leaf");
    if(do_free){
      uint64 pa = PTE2PA(*pte);
      kfree((void*)pa);
    }
    *pte = 0; // 将 PTE 标记为无效
  }
}

// 创建一个空的用户页表。
// 如果内存不足，返回 0。
pagetable_t
uvmcreate()
{
  pagetable_t pagetable;
  pagetable = (pagetable_t) kalloc();
  if(pagetable == 0)
    return 0;
  memset(pagetable, 0, PGSIZE);
  return pagetable;
}

// 为第一个进程加载 initcode 到页表的地址 0。
// sz 必须小于一个页面。
void
uvmfirst(pagetable_t pagetable, uchar *src, uint sz)
{
  char *mem;

  if(sz >= PGSIZE)
    panic("uvmfirst: more than a page");
  mem = kalloc();
  memset(mem, 0, PGSIZE);
  mappages(pagetable, 0, PGSIZE, (uint64)mem, PTE_W|PTE_R|PTE_X|PTE_U);
  memmove(mem, src, sz);
}

// 分配 PTE 和物理内存，将进程大小从 oldsz 增长到 newsz。
// newsz 不需要页对齐。成功时返回 newsz，错误时返回 0。
uint64
uvmalloc(pagetable_t pagetable, uint64 oldsz, uint64 newsz, int xperm)
{
  char *mem;
  uint64 a;

  if(newsz < oldsz)
    return oldsz;

  oldsz = PGROUNDUP(oldsz);
  for(a = oldsz; a < newsz; a += PGSIZE){
    mem = kalloc();
    if(mem == 0){
      uvmdealloc(pagetable, a, oldsz);
      return 0;
    }
    memset(mem, 0, PGSIZE);
    if(mappages(pagetable, a, PGSIZE, (uint64)mem, PTE_R|PTE_U|xperm) != 0){
      kfree(mem);
      uvmdealloc(pagetable, a, oldsz);
      return 0;
    }
  }
  return newsz;
}

// 释放用户页，将进程大小从 oldsz 减小到 newsz。
// oldsz 和 newsz 不需要页对齐，newsz 也不必小于 oldsz。
// oldsz 可能比实际进程大小要大。返回新的进程大小。
uint64
uvmdealloc(pagetable_t pagetable, uint64 oldsz, uint64 newsz)
{
  if(newsz >= oldsz)
    return oldsz;

  if(PGROUNDUP(newsz) < PGROUNDUP(oldsz)){
    int npages = (PGROUNDUP(oldsz) - PGROUNDUP(newsz)) / PGSIZE;
    uvmunmap(pagetable, PGROUNDUP(newsz), npages, 1);
  }

  return newsz;
}

// 递归地释放页表页。
// 所有叶子映射必须已经被移除。
void
freewalk(pagetable_t pagetable)
{
  // 一个页表中有 2^9 = 512 个 PTE。
  for(int i = 0; i < 512; i++){
    pte_t pte = pagetable[i];
    if((pte & PTE_V) && (pte & (PTE_R|PTE_W|PTE_X)) == 0){
      // 这个 PTE 指向一个更低级别的页表。
      uint64 child = PTE2PA(pte);
      freewalk((pagetable_t)child);
      pagetable[i] = 0;
    } else if(pte & PTE_V){
      // 这是一个叶子 PTE，不应该出现在这里。
      panic("freewalk: leaf");
    }
  }
  kfree((void*)pagetable);
}

// 释放用户内存页，然后释放页表页。
void
uvmfree(pagetable_t pagetable, uint64 sz)
{
  if(sz > 0)
    uvmunmap(pagetable, 0, PGROUNDUP(sz)/PGSIZE, 1);
  freewalk(pagetable);
}

// 给定父进程的页表，将其内存复制到子进程的页表中。
// 同时复制页表和物理内存。
// 成功返回 0，失败返回 -1。
// 失败时释放任何已分配的页面。
int
uvmcopy(pagetable_t old, pagetable_t new, uint64 sz)
{
  pte_t *pte;
  uint64 pa, i;
  uint flags;
  char *mem;

  for(i = 0; i < sz; i += PGSIZE){
    if((pte = walk(old, i, 0)) == 0)
      panic("uvmcopy: pte should exist");
    if((*pte & PTE_V) == 0)
      panic("uvmcopy: page not present");
    pa = PTE2PA(*pte);
    flags = PTE_FLAGS(*pte);
    if((mem = kalloc()) == 0)
      goto err;
    memmove(mem, (char*)pa, PGSIZE);
    if(mappages(new, i, PGSIZE, (uint64)mem, flags) != 0){
      kfree(mem);
      goto err;
    }
  }
  return 0;

 err:
  uvmunmap(new, 0, i / PGSIZE, 1);
  return -1;
}

// 将一个 PTE 标记为对用户不可访问。
// exec() 用它来设置用户栈的保护页 (guard page)。
void
uvmclear(pagetable_t pagetable, uint64 va)
{
  pte_t *pte;
  
  pte = walk(pagetable, va, 0);
  if(pte == 0)
    panic("uvmclear");
  *pte &= ~PTE_U; // 清除 PTE_U 位
}

// 从内核复制到用户空间。
// 将 len 字节从 src 复制到给定页表中的虚拟地址 dstva。
// 成功返回 0，错误返回 -1。
int
copyout(pagetable_t pagetable, uint64 dstva, char *src, uint64 len)
{
  uint64 n, va0, pa0;
  pte_t *pte;

  while(len > 0){
    va0 = PGROUNDDOWN(dstva);
    if(va0 >= MAXVA)
      return -1;
    pte = walk(pagetable, va0, 0);
    // 目标页面必须存在，可访问，且可写
    if(pte == 0 || (*pte & PTE_V) == 0 || (*pte & PTE_U) == 0 ||
       (*pte & PTE_W) == 0)
      return -1;
    pa0 = PTE2PA(*pte);
    // 计算可以在当前页内复制的字节数
    n = PGSIZE - (dstva - va0);
    if(n > len)
      n = len;
    memmove((void *)(pa0 + (dstva - va0)), src, n);

    len -= n;
    src += n;
    dstva = va0 + PGSIZE; // 移动到下一页的起始位置
  }
  return 0;
}

// 从用户空间复制到内核。
// 将给定页表中虚拟地址 srcva 的 len 字节复制到 dst。
// 成功返回 0，错误返回 -1。
int
copyin(pagetable_t pagetable, char *dst, uint64 srcva, uint64 len)
{
  uint64 n, va0, pa0;

  while(len > 0){
    va0 = PGROUNDDOWN(srcva);
    pa0 = walkaddr(pagetable, va0); // 查找物理地址
    if(pa0 == 0)
      return -1;
    n = PGSIZE - (srcva - va0);
    if(n > len)
      n = len;
    memmove(dst, (void *)(pa0 + (srcva - va0)), n);

    len -= n;
    dst += n;
    srcva = va0 + PGSIZE;
  }
  return 0;
}

// 从用户空间复制一个以 null 结尾的字符串到内核。
// 从给定页表的虚拟地址 srcva 复制字节到 dst，直到遇到 '\0' 或达到最大长度 max。
// 成功返回 0，错误返回 -1。
int
copyinstr(pagetable_t pagetable, char *dst, uint64 srcva, uint64 max)
{
  uint64 n, va0, pa0;
  int got_null = 0;

  while(got_null == 0 && max > 0){
    va0 = PGROUNDDOWN(srcva);
    pa0 = walkaddr(pagetable, va0);
    if(pa0 == 0)
      return -1;
    n = PGSIZE - (srcva - va0);
    if(n > max)
      n = max;

    char *p = (char *) (pa0 + (srcva - va0));
    while(n > 0){
      if(*p == '\0'){
        *dst = '\0';
        got_null = 1;
        break;
      } else {
        *dst = *p;
      }
      --n;
      --max;
      p++;
      dst++;
    }

    srcva = va0 + PGSIZE;
  }
  if(got_null){
    return 0;
  } else {
    // 未找到 null 终止符
    return -1;
  }
}
