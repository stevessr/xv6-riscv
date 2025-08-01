#include "param.h"
#include "types.h"
#include "memlayout.h"
#include "elf.h"
#include "riscv.h"
#include "defs.h"
#include "fs.h"

/*
 * xv6 虚拟内存管理 (Virtual Memory Management)
 *
 * RISC-V 架构使用多级页表来实现虚拟地址到物理地址的转换。
 * xv6 在其上运行的 QEMU 'virt' 机器上采用了 Sv39 分页模式，这意味着
 * 虚拟地址是 39 位宽，支持三级页表结构。
 *
 * 一个 64 位的虚拟地址在 Sv39 模式下被解释如下:
 * | 63..39 | 38..30 | 30..21 | 21..12 | 11..0      |
 * |--------|--------|--------|--------|------------|
 * | (未使用)| L2索引 | L1索引 | L0索引 | 页内偏移   |
 *
 * - bits 63-39: 必须与 bit 38 相同 (符号扩展)，在 xv6 中通常为 0，因为虚拟地址空间只使用了低39位。
 * - bits 38-30: 9位，用作顶级（Level-2）页表的索引。
 * - bits 29-21: 9位，用作中级（Level-1）页表的索引。
 * - bits 20-12: 9位，用作底层（Level-0）页表的索引。
 * - bits 11-0:  12位，用作页内偏移 (offset)，决定一个字节在 4096 字节 (2^12) 页内的具体位置。
 *
 * 一个页表页 (page-table page) 本身大小为 4KB，包含 512 (2^9) 个页表项 (PTE)。
 * 每个 PTE 是 64 位（8字节）宽，包含一个 44 位的物理页号 (PPN) 和一些标志位 (flags)。
 * PPN 指向下一级页表或最终的数据页的物理地址。PTE 中的标志位定义了访问权限（读、写、执行）、有效性等。
 */

/*
 * 全局唯一的内核页表。
 * 每个进程都有自己的用户页表，但所有进程在内核态时都共享这一个内核页表。
 */
pagetable_t kernel_pagetable;

extern char etext[];  // kernel.ld 中定义的符号，表示内核 .text 段（代码段）的结束地址。

extern char trampoline[]; // trampoline.S 中定义的trampoline代码的起始地址。

// 创建并初始化内核页表。
// 这个函数只在系统启动时被调用，用来建立内核地址空间。
// 它创建一个页表，其中包含了对硬件设备、内核代码和数据、以及物理内存的直接映射。
pagetable_t
kvmmake(void)
{
  pagetable_t kpgtbl;

  // 分配一个物理页作为 L2 (顶级) 页表。
  kpgtbl = (pagetable_t) kalloc();
  // 将新分配的页表清零。
  memset(kpgtbl, 0, PGSIZE);

  // --- 映射硬件设备 (Memory-Mapped I/O) ---
  // 这些设备的寄存器被映射到物理内存的特定地址，内核通过读写这些地址来控制它们。
  // 内核页表需要创建相应的映射，以便内核代码可以像访问内存一样访问这些设备寄存器。

  // 映射 UART (串口) 寄存器。 UART0 是其物理地址。
  // 虚拟地址和物理地址相同，大小为一个页，权限为可读可写。
  kvmmap(kpgtbl, UART0, UART0, PGSIZE, PTE_R | PTE_W);

  // 映射 virtio MMIO (内存映射 I/O) 磁盘接口。
  kvmmap(kpgtbl, VIRTIO0, VIRTIO0, PGSIZE, PTE_R | PTE_W);

  // 映射 SiFive 测试设备，用于关机
  kvmmap(kpgtbl, SIFIVE_TEST, SIFIVE_TEST, PGSIZE, PTE_R | PTE_W);

  // 映射 PLIC (平台级中断控制器)。
  // PLIC 的地址空间较大，这里映射了 0x400000 字节。
  kvmmap(kpgtbl, PLIC, PLIC, 0x400000, PTE_R | PTE_W);

  // --- 映射内核内存 ---
  // 内核虚拟地址从 KERNBASE (0x80000000) 开始，直接映射到同值的物理地址。
  // 这种映射方式称为直接映射 (direct mapping)。

  // 映射内核代码段 (.text)，权限为可读(R)和可执行(X)。
  // 从 KERNBASE 到 etext 的区域是内核指令。
  kvmmap(kpgtbl, KERNBASE, KERNBASE, (uint64)etext-KERNBASE, PTE_R | PTE_X);

  // 映射从 etext 到 PHYSTOP 的物理内存，包括内核数据段、BSS段和剩余的RAM。
  // 这部分内存区域权限为可读(R)和可写(W)，不可执行。
  kvmmap(kpgtbl, (uint64)etext, (uint64)etext, PHYSTOP-(uint64)etext, PTE_R | PTE_W);

  // 映射 trampoline 页。
  // trampoline 页被映射到虚拟地址空间的顶部 (TRAMPOLINE)。
  // 它包含在用户态和内核态之间切换所需的代码 (uservec 和 userret)。
  // 这个页面必须在所有地址空间（内核和用户）中都以相同的虚拟地址映射，
  // 且权限为可读(R)和可执行(X)，以便在切换时能够正确执行。
  kvmmap(kpgtbl, TRAMPOLINE, (uint64)trampoline, PGSIZE, PTE_R | PTE_X);

  // 为每个可能的进程分配并映射其内核栈。
  // 每个进程都有一个独立的内核栈，在内核态时使用。
  proc_mapstacks(kpgtbl);
  
  return kpgtbl;
}

// 初始化内核页表 (在第一个CPU上执行)
// 这个函数调用 kvmmake() 来创建页表，并将结果存储在全局变量 kernel_pagetable 中。
void
kvminit(void)
{
  kernel_pagetable = kvmmake();
}

// 在每个 hart (硬件线程/CPU核心) 上启用分页。
// Switch h/w page table register to the kernel's page table,
// and enable paging.
void
kvminithart()
{
  // 等待所有对页表内存的写入操作完成，确保页表内容已经写入主存。
  // 这是一个内存屏障指令。
  sfence_vma();

  // 将内核页表的根物理地址写入 satp (Supervisor Address Translation and Protection) 寄存器。
  // `MAKE_SATP` 宏将页表基地址和分页模式 (Sv39) 组合成 satp 需要的格式。
  // 一旦 satp 被设置，CPU 的 MMU (内存管理单元) 就正式开始使用这个页表进行地址转换。
  w_satp(MAKE_SATP(kernel_pagetable));

  // 刷新 TLB (Translation Lookaside Buffer)。
  // TLB 是地址转换的硬件缓存。切换页表后，旧的缓存条目可能不再有效。
  // `sfence_vma` 指令会清空整个 TLB，确保后续的内存访问使用新的页表进行转换，
  // 避免使用陈旧的映射。
  sfence_vma();
}

// 在页表中查找虚拟地址 `va` 对应的 PTE 的地址。
// 如果 `alloc` 非零，则在遍历过程中如果遇到缺失的页表页，会自动分配。
//
// The risc-v Sv39 scheme has three levels of page-table
// pages. A page-table page contains 512 64-bit PTEs.
// A 64-bit virtual address is split into five fields:
//   39..63 -- must be zero.
//   30..38 -- 9 bits of level-2 index.
//   21..29 -- 9 bits of level-1 index.
//   12..20 -- 9 bits of level-0 index.
//    0..11 -- 12 bits of byte offset.
pte_t *
walk(pagetable_t pagetable, uint64 va, int alloc)
{
  // 检查虚拟地址是否超出了最大合法范围
  if(va >= MAXVA)
    panic("walk");

  // 从顶层(L2)开始，逐级向下遍历页表。
  for(int level = 2; level > 0; level--) {
    // PX(level, va) 宏从 `va` 中提取当前级别的页表索引。
    pte_t *pte = &pagetable[PX(level, va)];
    
    // 检查 PTE 是否有效 (PTE_V 位)。
    if(*pte & PTE_V) {
      // 如果有效，说明它指向下一级的页表。
      // PTE2PA 宏从 PTE 中提取物理地址，并将其转换为内核可用的指针。
      pagetable = (pagetable_t)PTE2PA(*pte);
    } else {
      // 如果 PTE 无效，表示下一级页表不存在。
      // 如果 alloc 为假，或者分配新页失败，则返回0。
      if(!alloc || (pagetable = (pde_t*)kalloc()) == 0)
        return 0;
      
      // 将新分配的页清零。
      memset(pagetable, 0, PGSIZE);
      // PA2PTE 宏将新页的物理地址转换为 PTE 格式，并设置有效位。
      // 这个新的 PTE 指向新创建的下一级页表。
      *pte = PA2PTE(pagetable) | PTE_V;
    }
  }
  
  // 循环结束后, `pagetable` 指向最底层的 L0 页表。
  // 返回 va 在 L0 页表中对应的 PTE 的地址。这个 PTE 指向最终的数据页。
  return &pagetable[PX(0, va)];
}

// 查找一个虚拟地址对应的物理地址。
// 它通过 walk() 找到对应的 PTE，然后从中提取物理地址。
// 这个函数只应该用于查找用户地址，因为它会检查 PTE_U 标志。
// 如果找不到映射或页面不是用户页，则返回 0。
uint64
walkaddr(pagetable_t pagetable, uint64 va)
{
  pte_t *pte;
  uint64 pa;

  if(va >= MAXVA)
    return 0;

  // 遍历页表找到对应的 L0 PTE，但不创建新页表 (alloc=0)。
  pte = walk(pagetable, va, 0);
  if(pte == 0) // walk 失败，中间页表不存在
    return 0;
  if((*pte & PTE_V) == 0) // L0 PTE 无效，页面未映射
    return 0;
  if((*pte & PTE_U) == 0) // 页面不是用户页 (PTE_U 位未设置)，禁止内核访问
    return 0;
  
  // 从有效的用户页 PTE 中提取物理地址。
  pa = PTE2PA(*pte);
  return pa;
}

// 在内核页表中添加一个映射。这是一个 mappages 的简单包装器。
// 它仅在启动时 (during boot) 由 kvmmake() 调用。
// 因为此时分页尚未在硬件中启用，所以不需要手动刷新 TLB。
// 如果 mappages 失败，系统将 panic，因为内核页表的建立是关键过程。
void
kvmmap(pagetable_t kpgtbl, uint64 va, uint64 pa, uint64 sz, int perm)
{
  if(mappages(kpgtbl, va, sz, pa, perm) != 0)
    panic("kvmmap");
}

// 在指定的页表中创建一段地址映射。
// 它将从虚拟地址 `va` 开始，大小为 `size` 的地址空间，
// 映射到从物理地址 `pa` 开始的物理内存。
// `va` 和 `pa` 不必页对齐，函数内部会处理对齐。
// 返回 0 表示成功，-1 表示 walk() 无法分配所需的页表页。
int
mappages(pagetable_t pagetable, uint64 va, uint64 size, uint64 pa, int perm)
{
  uint64 a, last;
  pte_t *pte;

  if(size == 0)
    panic("mappages: size is 0");
  
  // 将起始虚拟地址向下对齐到页边界。
  a = PGROUNDDOWN(va);
  // 计算并向下对齐最后一个需要映射的虚拟地址。
  last = PGROUNDDOWN(va + size - 1);
  for(;;){
    // 获取当前虚拟地址 `a` 对应的 PTE 的地址。如果中间页表不存在，则创建 (alloc=1)。
    if((pte = walk(pagetable, a, 1)) == 0)
      return -1; // 分配页表失败
    if(*pte & PTE_V)
      panic("mappages: remap"); // 试图重新映射一个已经存在的页面，这通常是一个 bug
    
    // 设置 PTE：将物理地址 `pa` 转换为 PTE 格式，并合并权限 `perm` 和有效位 `PTE_V`。
    *pte = PA2PTE(pa) | perm | PTE_V;
    
    // 如果已经处理完最后一个页面，则退出循环。
    if(a == last)
      break;
    
    // 移动到下一个虚拟页和物理页。
    a += PGSIZE;
    pa += PGSIZE;
  }
  return 0;
}

// 从一个用户页表中移除一段映射。
// `va` 必须页对齐。`npages` 是要移除的页数。
// 如果 `do_free` 非零，则同时释放这些页对应的物理内存。
// Remove npages of mappings starting from va. va must be
// page-aligned. The mappings must exist.
// Optionally free the physical memory.
void
uvmunmap(pagetable_t pagetable, uint64 va, uint64 npages, int do_free)
{
  uint64 a;
  pte_t *pte;

  if((va % PGSIZE) != 0)
    panic("uvmunmap: not aligned");

  // 遍历所有需要解除映射的页面
  for(a = va; a < va + npages*PGSIZE; a += PGSIZE){
    // 查找 PTE，不分配新页表。如果找不到，说明代码有逻辑错误。
    if((pte = walk(pagetable, a, 0)) == 0)
      panic("uvmunmap: walk");
    // 如果页面本来就未映射，也是逻辑错误。
    if((*pte & PTE_V) == 0)
      panic("uvmunmap: not mapped");
    // 检查 PTE 是否是叶子节点。如果它指向另一个页表，则是错误。
    if(PTE_FLAGS(*pte) == PTE_V)
      panic("uvmunmap: not a leaf");
    
    // 如果需要释放物理内存
    if(do_free){
      uint64 pa = PTE2PA(*pte);
      kfree((void*)pa);
    }
    // 将 PTE 清零，使其无效。这样就移除了映射。
    // TLB 会在返回用户空间时被刷新。
    *pte = 0;
  }
}

// 创建一个空的用户页表。
// 只是分配一个页作为顶级(L2)页表，并将其清零。
// 如果内存分配失败则返回 0。
pagetable_t
uvmcreate()
{
  pagetable_t pagetable;
  // 分配一个页作为顶级页表。
  pagetable = (pagetable_t) kalloc();
  if(pagetable == 0)
    return 0;
  memset(pagetable, 0, PGSIZE);
  return pagetable;
}

// 为第一个用户进程（initcode）加载代码和数据。
// 这个函数分配一页物理内存，将 `initcode.S` 的内容复制进去，
// 然后将其映射到页表 `pagetable` 的虚拟地址 0。
// sz 必须小于一个页的大小。
void
uvmfirst(pagetable_t pagetable, uchar *src, uint sz)
{
  char *mem;

  if(sz >= PGSIZE)
    panic("uvmfirst: more than a page");
  // 分配一页物理内存
  mem = kalloc();
  // 将该页清零
  memset(mem, 0, PGSIZE);
  // 将该页映射到虚拟地址0，权限为 可读|可写|可执行|用户页
  mappages(pagetable, 0, PGSIZE, (uint64)mem, PTE_W|PTE_R|PTE_X|PTE_U);
  // 将 initcode 的内容复制到新分配的页中
  memmove(mem, src, sz);
}

// 为用户进程分配并映射新的内存页，使进程大小从 `oldsz` 增长到 `newsz`。
// oldsz 和 newsz 不必是页对齐的。
// 成功时返回 newsz，失败时返回 0。如果失败，会释放已分配的内存。
uint64
uvmalloc(pagetable_t pagetable, uint64 oldsz, uint64 newsz, int xperm)
{
  char *mem;
  uint64 a;

  if(newsz < oldsz)
    return oldsz;

  // 将 oldsz 向上对齐到页边界，从对齐后的地址开始分配
  oldsz = PGROUNDUP(oldsz);
  for(a = oldsz; a < newsz; a += PGSIZE){
    // 分配一页物理内存
    mem = kalloc();
    if(mem == 0){
      // 如果分配失败，则回滚，释放从 oldsz 到 a 已经分配的所有页
      uvmdealloc(pagetable, a, oldsz);
      return 0;
    }
    // 将新页清零
    memset(mem, 0, PGSIZE);
    
    // 将新分配的物理页 mem 映射到虚拟地址 a。
    // 权限包括读、用户，以及由 xperm 指定的执行权限。
    if(mappages(pagetable, a, PGSIZE, (uint64)mem, PTE_R|PTE_U|xperm) != 0){
      // 如果映射失败，释放刚刚分配的物理页并回滚
      kfree(mem);
      uvmdealloc(pagetable, a, oldsz);
      return 0;
    }
  }
  return newsz;
}

// 释放用户进程的内存，将进程大小从 `oldsz` 缩小到 `newsz`。
// oldsz 和 newsz 不必页对齐。
// 返回 newsz。
uint64
uvmdealloc(pagetable_t pagetable, uint64 oldsz, uint64 newsz)
{
  if(newsz >= oldsz)
    return oldsz;

  // 如果需要释放至少一个完整的页面
  if(PGROUNDUP(newsz) < PGROUNDUP(oldsz)){
    // 计算需要释放的页数
    int npages = (PGROUNDUP(oldsz) - PGROUNDUP(newsz)) / PGSIZE;
    // 从向上对齐的 newsz 地址开始，解除映射并释放物理内存 (do_free=1)
    uvmunmap(pagetable, PGROUNDUP(newsz), npages, 1);
  }

  return newsz;
}

// 递归地释放一个页表的所有页。
// 它会遍历一个页表页中的所有PTE。如果PTE指向一个子页表，
// 它会递归调用自身来释放那个子页表，然后释放该PTE所在的页表页本身。
// 调用此函数前，所有叶子节点（指向数据页的PTE）必须已经被 `uvmunmap` 清除。
void
freewalk(pagetable_t pagetable)
{
  // 一个页表中有 512 (2^9) 个 PTE。
  for(int i = 0; i < 512; i++){
    pte_t pte = pagetable[i];
    // 一个 PTE 如果有效(V=1)，但没有读/写/执行权限(R=W=X=0)，
    // 那么它一定是指向下一级页表的指针。
    if((pte & PTE_V) && (pte & (PTE_R|PTE_W|PTE_X)) == 0){
      // this PTE points to a lower-level page table.
      uint64 child = PTE2PA(pte);
      freewalk((pagetable_t)child); // 递归释放子页表
      pagetable[i] = 0; // 清除当前PTE
    } else if(pte & PTE_V){
      // 如果PTE有效且拥有R/W/X权限中的任何一个，它就是一个叶子节点。
      // freewalk 不应该遇到叶子节点，否则说明有内存泄漏。
      panic("freewalk: leaf");
    }
  }
  // 释放页表页本身
  kfree((void*)pagetable);
}

// 释放一个用户页表以及其引用的所有物理内存。
// 首先使用 uvmunmap 释放所有用户数据页（从va=0到sz），
// 然后使用 freewalk 释放所有页表页本身。
void
uvmfree(pagetable_t pagetable, uint64 sz)
{
  if(sz > 0)
    // 解除从虚拟地址0开始，大小为sz的区域的所有映射，并释放物理页 (do_free=1)
    uvmunmap(pagetable, 0, PGROUNDUP(sz)/PGSIZE, 1);
  // 释放页表本身
  freewalk(pagetable);
}

// 将父进程的内存空间完整地复制到子进程的页表中。
// 用于 `fork()` 系统调用。
// 它会遍历父进程的用户页表，对于每一个映射的用户页：
// 1. 分配一个新的物理页。
// 2. 将父进程页的内容复制到新页。
// 3. 在子进程的新页表中为新页创建相同的映射。
// 成功返回0，失败返回-1。失败时会释放已分配的资源。
int
uvmcopy(pagetable_t old, pagetable_t new, uint64 sz)
{
  pte_t *pte;
  uint64 pa, i;
  uint flags;
  char *mem;

  // 遍历父进程的整个用户地址空间
  for(i = 0; i < sz; i += PGSIZE){
    // 查找父进程中虚拟地址i对应的PTE
    if((pte = walk(old, i, 0)) == 0)
      panic("uvmcopy: pte should exist");
    // 如果该页未被映射，也是一个错误
    if((*pte & PTE_V) == 0)
      panic("uvmcopy: page not present");
    
    // 获取物理地址和权限标志
    pa = PTE2PA(*pte);
    flags = PTE_FLAGS(*pte);
    
    // 为子进程分配一个新的物理页
    if((mem = kalloc()) == 0)
      goto err; // 分配失败，跳转到错误处理
      
    // 将父进程的物理页内容复制到子进程的新物理页
    memmove(mem, (char*)pa, PGSIZE);
    
    // 在子进程的页表中，将虚拟地址i映射到新分配的物理页
    if(mappages(new, i, PGSIZE, (uint64)mem, flags) != 0){
      kfree(mem); // 映射失败，释放物理页并跳转到错误处理
      goto err;
    }
  }
  return 0;

// 错误处理：释放子进程页表中已经复制的所有页
 err:
  uvmunmap(new, 0, i / PGSIZE, 1);
  return -1;
}

// 将一个页表项(PTE)的 PTE_U (User) 位清除。
// 这使得该页对用户代码不可访问。
// `exec()` 系统调用用它来创建用户栈下方的保护页 (guard page)，
// 防止栈溢出破坏其他内存区域。
void
uvmclear(pagetable_t pagetable, uint64 va)
{
  pte_t *pte;
  
  pte = walk(pagetable, va, 0);
  if(pte == 0)
    panic("uvmclear");
  // 通过位运算清除 PTE_U 标志位
  *pte &= ~PTE_U;
}

// 安全地将数据从内核空间复制到用户空间。
// 它逐页地翻译用户虚拟地址，然后执行复制。
// 这是必要的，因为内核不能直接解引用用户指针。
// Return 0 on success, -1 on error.
int
copyout(pagetable_t pagetable, uint64 dstva, char *src, uint64 len)
{
  uint64 n, va0, pa0;
  pte_t *pte;

  while(len > 0){
    // 将目标虚拟地址向下对齐，找到它所在的页的起始地址
    va0 = PGROUNDDOWN(dstva);
    // 检查目标虚拟地址是否越界 (例如，超过MAXVA或进入内核空间)
    if(va0 >= MAXVA)
      return -1;
    // 查找该用户虚拟页对应的物理地址
    pte = walk(pagetable, va0, 0);
    // 目标页面必须存在，可访问，且可写
    if(pte == 0 || (*pte & PTE_V) == 0 || (*pte & PTE_U) == 0 ||
       (*pte & PTE_W) == 0)
      return -1; // 地址未映射或非用户页
    pa0 = PTE2PA(*pte);
    // 计算当前页内还能复制多少字节
    n = PGSIZE - (dstva - va0);
    if(n > len) // 如果剩余要复制的字节数小于页内剩余空间
      n = len;
    
    // 计算目标物理地址并执行复制
    memmove((void *)(pa0 + (dstva - va0)), src, n);

    // 更新剩余长度、源地址和目标虚拟地址
    len -= n;
    src += n;
    dstva = va0 + PGSIZE; // 移动到下一页的起始位置
  }
  return 0;
}

// 安全地将数据从用户空间复制到内核空间。
// 逻辑与 copyout 非常相似，只是数据流动的方向相反。
// Return 0 on success, -1 on error.
int
copyin(pagetable_t pagetable, char *dst, uint64 srcva, uint64 len)
{
  uint64 n, va0, pa0;

  while(len > 0){
    // 将源虚拟地址向下对齐，找到它所在的页的起始地址
    va0 = PGROUNDDOWN(srcva);
    // 查找该用户虚拟页对应的物理地址
    pa0 = walkaddr(pagetable, va0);
    if(pa0 == 0)
      return -1; // 地址未映射、非用户页或越界
      
    // 计算当前页内还能复制多少字节
    n = PGSIZE - (srcva - va0);
    if(n > len)
      n = len;
      
    // 计算源物理地址并执行复制
    memmove(dst, (void *)(pa0 + (srcva - va0)), n);

    // 更新剩余长度、目标地址和源虚拟地址
    len -= n;
    dst += n;
    srcva = va0 + PGSIZE;
  }
  return 0;
}

// 安全地从用户空间复制一个以 null 结尾的字符串到内核空间。
// 类似于 `copyin`，但它会逐字节检查空字符 `\0`，并且有最大复制长度 `max` 的限制。
// Return 0 on success, -1 on error.
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
      
    // 计算本页内可检查的最大字节数
    n = PGSIZE - (srcva - va0);
    if(n > max) // 不能超过 `max` 限制
      n = max;

    // 获取源物理地址
    char *p = (char *) (pa0 + (srcva - va0));
    while(n > 0){
      if(*p == '\0'){
        *dst = '\0';
        got_null = 1; // 找到 null 终止符
        break;
      } else {
        *dst = *p; // 复制字符
      }
      // 更新计数器和指针
      --n;
      --max;
      p++;
      dst++;
    }

    // 移动到下一页继续查找
    srcva = va0 + PGSIZE;
  }
  
  if(got_null){
    return 0; // 成功找到并复制了完整的字符串
  } else {
    return -1; // 在 `max` 字节内没有找到 null 终止符
  }
}
