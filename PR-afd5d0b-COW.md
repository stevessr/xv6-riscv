# PR 说明：提交 afd5d0b992cbd9054be6609f3231e678a77c01ff（COW 实现）

提交概览

- commit: afd5d0b992cbd9054be6609f3231e678a77c01ff
- author: stevessr <steve-ssr@outlook.com>
- date: Tue Sep 30 15:36:18 2025 +0800
- 摘要：实现写时复制（COW）机制，增加物理页的引用计数管理
- 影响文件：
  - 新增 `COW_IMPLEMENTATION.md`
  - 修改 `kernel/riscv.h`
  - 修改 `kernel/trap.c`
  - 修改 `kernel/vm.c`
  - 新增 `time.txt`

下面包含每个被修改/新增文件的完整代码更改（patch 中新增行）以及逐行解释和变量/函数用途说明。

---

## 1) 新增文件：`COW_IMPLEMENTATION.md`

（文件为文档性内容，完整新增文本如下）

# xv6-riscv: 写时复制 (COW) 实现说明

**日期**：2025-10-09

## 概述
在 xv6-riscv 内核中实现了 fork 的写时复制（Copy-On-Write, COW）。父子在 fork 时共享可写用户页（用 `PTE_COW` 标
记并清写权限），通过物理页引用计数延迟复制；只有在实际写发生时才复制物理页。                                    
在实现过程中修复了边界与并发问题，并使用仓库自带测试完成回归（最终通过，Score 130/130）。

本文档包含：变更要点、关键数据结构、主要流程、不变式、遇到的问题与修复、回归测试、复现步骤与后续建议。

## 变更要点（文件）

- `kernel/vm.c`
  - 新增 `refcount[NPHYSPAGES]`；新增 `struct spinlock ref_lock` 并在 `kvminit()` 初始化。
  - `increfpa` / `decrefpa`：在 `ref_lock` 保护下维护引用计数，`decrefpa` 在计数降为 0 时释放物理页。
  - `uvmcopy`：把已标记为 COW 或可写的用户页共享给子进程（清写位、置 `PTE_COW`、`increfpa(pa)`），避免不必要拷
贝。                                                                                                            
  - `uvmunmap`：释放映射时改为调用 `decrefpa(pa)`（不直接 `kfree`）。

- `kernel/trap.c`
  - 在 `usertrap()` 的 store page fault 分支实现 COW 处理：在 `ref_lock` 下读取 `refcount`，根据计数决定复制或
恢复写权限；复制后 `sfence_vma()`。
  - 在处理前过滤 `va0 >= MAXVA`（越界地址），避免 `walk()` panic。

- `kernel/riscv.h`：新增 `PTE_COW`（软件位，例如 `1 << 8`）。

## 关键数据结构与常量

- `refcount[NPHYSPAGES]`：物理页引用计数，索引 `idx = (pa - KERNBASE) / PGSIZE`。
- `ref_lock`：自旋锁，保护 `refcount`。
- `PTE_COW`：PTE 中的软件位，用来标记 COW 页面。

## 主要流程（摘要）

1. fork (`uvmcopy`)：把可写用户页或已 COW 的页映射给子进程，清写位并置 `PTE_COW`，调用 `increfpa(pa)`。
2. 用户写触发 store page fault：
   - 若 `va0 >= MAXVA`：kill 进程（保护 `walk()` 不被越界调用）。
   - 否则若 PTE 标记 `PTE_COW`：在 `ref_lock` 下读 `refcount`：
     - `cnt > 1`：分配新页、memmove、`decrefpa(old)`、`increfpa(new)`、更新 PTE 为新页并设写权限，执行 `sfence_
vma()`。
     - `cnt == 1`：直接在 PTE 中恢复写权限并清除 `PTE_COW`（无需物理复制）。
3. 释放：通过 `decrefpa` 管理物理页释放；只有计数为 0 时才调用 `kfree`。

## 不变式与注意点

- 对 `PTE_COW` 页，PTE_W 应为 0（写权限由缺页处理恢复）。
- `refcount` 的每次修改应在 `ref_lock` 保护下完成。
- `pa` 到 `refcount` 索引的计算假定内核采用 direct mapping（以 `KERNBASE` 为偏移）。

## 遇到的问题与解决

- MAXVA 越界导致 `walk()` panic：在 trap 处理里先判断 `va0 >= MAXVA` 并 kill，避免直接调用 `walk()`。
- `uvmcopy` 对已 COW 页重复分配导致引用计数不一致：修改为共享已 COW 页并 `increfpa(pa)`。
- `refcount` 并发竞态：引入 `ref_lock` 并在 `increfpa`/`decrefpa` 与 trap 读取时加锁。

## 回归测试与复现步骤

在修改完成后使用仓库自带脚本运行完整回归：

```fish
cd /home/steve/xv6/xv6-riscv
make -j4
./grade-lab-cow
```

结果：`cowtest` 与 `usertests` 全通过，最终得分 `130/130`。

## 已知限制与后续改进建议

- `ref_lock` 是单一全局锁，正确但在高并发时可能成为瓶颈。可考虑分段锁或使用原子计数以减少争用。
- 建议把 `PTE_COW` 与 `refcount` 的语义写入代码注释与项目文档，便于维护。
- 建议添加并发压力测试（多 hart 并行 fork/exit）以验证稳定性与性能。

## 提交与 PR 建议

- 将改动拆为 1–2 个清晰 commit：
  1. 引入 `refcount` 与基础接口（`increfpa`/`decrefpa`）；
  2. 实现 `uvmcopy` 的 COW 共享逻辑与 `usertrap` 的缺页处理并修复并发问题。

---

## 2) 修改文件：`kernel/riscv.h`（新增宏）

新增代码（patch 中新增行）：

```c
// use RSW (Reserved for Software) bits for COW
#define PTE_COW (1L << 8) // copy-on-write page
```

逐行说明：
- 注释：使用保留软件位（RSW）作为 COW 标志位。
- `#define PTE_COW (1L << 8)`：定义 PTE_COW 宏，被用来标记页为 COW。该位与其它 PTE 标志（如 PTE_W, PTE_U 等）共同组成 PTE 标志掩码。

用途/影响：内核中读取/修改 PTE 时会用到该宏来判断或设置 COW 行为（例如在 `usertrap`、`uvmcopy`、`copyout` 中）。

---

## 3) 修改文件：`kernel/trap.c`

在文件顶部新增 extern 声明：

```c
// COW helpers in vm.c
extern void increfpa(uint64 pa);
extern void decrefpa(uint64 pa);
// refcount lock in vm.c
extern struct spinlock ref_lock;
```

说明：
- `extern void increfpa(uint64 pa);` 与 `extern void decrefpa(uint64 pa);` 表明这两个函数在 `vm.c` 中定义，并在 `trap.c` 中被调用来维护物理页引用计数。
- `extern struct spinlock ref_lock;` 用于在 trap 中对 `refcount` 做安全读写。

在 `usertrap()` 的错误处理分支中新增对 store page fault（scause == 15）的特殊处理逻辑，新增代码完整段：

```c
uint64 scause = r_scause();
if(scause == 15){ // store page fault on RISC-V
  uint64 va = r_stval();
  uint64 va0 = PGROUNDDOWN(va);
  // If the faulting address is at or above MAXVA, do not call walk()
  // because walk() panics for va >= MAXVA. Instead, kill the process.
  if(va0 >= MAXVA){
    printf("usertrap(): write to invalid VA 0x%lx >= MAXVA 0x%lx pid=%d\n", va0, (uint64)MAXVA, p->pid);
    setkilled(p);
  } else {
    pte_t *pte = walk(p->pagetable, va0, 0);
    
    if(pte == 0 || (*pte & PTE_V) == 0 || (*pte & PTE_U) == 0){
      printf("usertrap(): unexpected scause 0x%lx pid=%d\n", scause, p->pid);
      printf("            sepc=0x%lx stval=0x%lx\n", r_sepc(), r_stval());
      setkilled(p);
    } else {
      uint64 pa = PTE2PA(*pte);
      uint flags = PTE_FLAGS(*pte);
      // if writable, shouldn't fault; check if COW page
      if((flags & PTE_COW) && (flags & PTE_W) == 0){
        // This is a COW page
        extern uint refcount[];
        uint64 idx = (pa - KERNBASE) / PGSIZE;
        // read refcount under lock to avoid races with other harts
        uint cnt;
        acquire(&ref_lock);
        cnt = refcount[idx];
        release(&ref_lock);
        if(cnt > 1){
          // allocate new page and copy
          char *mem = kalloc();
          if(mem == 0){
            setkilled(p);
          } else {
            memmove(mem, (char*)pa, PGSIZE);
            decrefpa(pa);
            increfpa((uint64)mem);
            // Remove COW flag and add write permission back
            flags = (flags & ~PTE_COW) | PTE_W;
            *pte = PA2PTE((uint64)mem) | flags;
            sfence_vma();
          }
        } else if(cnt == 1){
          // Only one reference, just make writable
          flags = (flags & ~PTE_COW) | PTE_W;
          *pte = PA2PTE(pa) | flags;
          sfence_vma();
        } else {
          // shouldn't happen
          printf("usertrap(): COW page with refcount error\n");
          setkilled(p);
        }
      } else {
        // Not a COW page but faulted
        printf("usertrap(): page fault not COW pid=%d\n", p->pid);
        printf("            sepc=0x%lx stval=0x%lx pte=0x%lx\n", r_sepc(), r_stval(), *pte);
        setkilled(p);
      }
    }
  }
} else {
  printf("usertrap(): unexpected scause 0x%lx pid=%d\n", r_scause(), p->pid);
  printf("            sepc=0x%lx stval=0x%lx\n", r_sepc(), r_stval());
  setkilled(p);
}
```

逐行/关键点说明（概要）：
- `scause == 15`：表示 store page fault（用户写引发）。
- `va = r_stval()`、`va0 = PGROUNDDOWN(va)`：读取并对齐故障虚拟地址。
- 越界保护：若 `va0 >= MAXVA` 则直接 `setkilled(p)` 防止 `walk()` panic。
- `pte = walk(p->pagetable, va0, 0)`：查找页表项，不分配。
- 基本校验：如果 PTE 无效或不是用户页（PTE_V / PTE_U 检查），打印信息并 kill。
- `pa = PTE2PA(*pte)`、`flags = PTE_FLAGS(*pte)`：获取物理地址与标志。
- 检查是否是 COW：`if((flags & PTE_COW) && (flags & PTE_W) == 0)`。若是：
  - 在 `ref_lock` 下读取 `refcount[idx]`（idx 由 `pa` 算出）。
  - 若 `cnt > 1`（共享页）：分配新页 `kalloc()`、memmove、`decrefpa(pa)`、`increfpa(new)`、更新 PTE 指向新页，并 `sfence_vma()`。
  - 若 `cnt == 1`：无需复制，直接在 PTE 中清除 `PTE_COW` 并设置 `PTE_W`，调用 `sfence_vma()`。
  - 否则（cnt < 1）视为错误并 kill。
- 非 COW 缺页则打印并 kill。

---

## 4) 修改文件：`kernel/vm.c`

该文件新增了 refcount 管理函数、变量，并修改了多个内存分配/释放路径以维护引用计数；同时 uvmcopy 改为 COW 行为，copyout 也支持 COW。

文件顶部新增包含和声明：

```c
#include "memlayout.h"
#include "elf.h"
#include "riscv.h"
#include "spinlock.h"
#include "defs.h"
#include "fs.h"

// reference count for physical pages (indexed by physical page number)
// We allocate enough space for all possible physical pages
#define NPHYSPAGES ((PHYSTOP - KERNBASE) / PGSIZE)
uint refcount[NPHYSPAGES];

// lock to protect refcount array in SMP
struct spinlock ref_lock;

// forward declarations
void increfpa(uint64 pa);
void decrefpa(uint64 pa);
```

解释：
- 引入 `spinlock.h` 以使用 `struct spinlock`。
- 定义 `NPHYSPAGES`，并为每个物理页分配 `refcount` 条目。
- `ref_lock` 用于保护对 refcount 的修改。
- 前向声明 `increfpa`/`decrefpa`，以便跨文件引用。

在 `kvminit()` 中初始化锁：

```c
initlock(&ref_lock, "refcount");
```

说明：在内核页表初始化时初始化 `ref_lock`。

修改 `walk()` 的越界提示：

```c
if(va >= MAXVA){
  // print the offending virtual address to help debug MAXVA-related panics
  printf("walk: va 0x%lx >= MAXVA 0x%lx\n", va, (uint64)MAXVA);
  panic("walk");
}
```

说明：在 panic 前打印越界 va，有助于定位触发位置。

在 `uvmunmap()` 释放映射时改为调用 `decrefpa`：

```c
if(do_free){
  uint64 pa = PTE2PA(*pte);
  // decrement reference count and free physical page if needed
  decrefpa(pa);
}
*pte = 0;
```

说明：不再直接 `kfree`，而是减少引用计数；只有计数为 0 时才真正释放物理页。

在 `uvmfirst()` 与 `uvmalloc()` 中对新分配页调用 `increfpa`：

```c
// track refcount for this newly allocated page
increfpa((uint64)mem);
```

说明：新分配页被映射后需要把引用计数置为 1。

新增 `increfpa` / `decrefpa` 的实现：

```c
void
increfpa(uint64 pa)
{
  if(pa >= PHYSTOP || pa < KERNBASE)
    return;
  uint64 idx = (pa - KERNBASE) / PGSIZE;
  if(idx >= NPHYSPAGES)
    panic("increfpa: out of range");
  acquire(&ref_lock);
  refcount[idx]++;
  release(&ref_lock);
}

void
decrefpa(uint64 pa)
{
  if(pa >= PHYSTOP || pa < KERNBASE)
    return;
  uint64 idx = (pa - KERNBASE) / PGSIZE;
  if(idx >= NPHYSPAGES)
    panic("decrefpa: out of range");
  acquire(&ref_lock);
  if(refcount[idx] > 0){
    refcount[idx]--;
    int needfree = 0;
    if(refcount[idx] == 0)
      needfree = 1;
    release(&ref_lock);
    if(needfree)
      kfree((void*)pa);
  } else {
    // if was never tracked, free directly
    release(&ref_lock);
    kfree((void*)pa);
  }
}
```

逐行说明：
- `increfpa`: 检查 pa 是否在管理范围内（KERNBASE <= pa < PHYSTOP），计算索引并在锁下自增 `refcount[idx]`。
- `decrefpa`: 同样检查范围并在锁下将 `refcount[idx]` 递减；若递减后为 0，则在释放锁后调用 `kfree((void*)pa)` 释放页面（避免持锁时调用较慢的 kfree）。
- 若 `refcount[idx]` 本来为 0，则认为页面未被跟踪，释放锁后直接 `kfree`（这是一种保底行为，但可能掩盖引用计数不一致的 bug）。

核心修改：`uvmcopy` 改为 COW 共享实现（整段重要代码）：

```c
// Implement copy-on-write: share pages between parent and child.
for(i = 0; i < sz; i += PGSIZE){
  if((pte = walk(old, i, 0)) == 0)
    panic("uvmcopy: pte should exist");
  if((*pte & PTE_V) == 0)
    panic("uvmcopy: page not present");
  pa = PTE2PA(*pte);
  flags = PTE_FLAGS(*pte);

  // If page is already COW, or is writable user page, share it (COW).
  if((flags & PTE_COW) || ((flags & PTE_W) && (flags & PTE_U))){
    // clear write permission and mark as COW
    uint newflags = (flags & ~PTE_W) | PTE_COW;

    if(mappages(new, i, PGSIZE, pa, newflags) != 0){
      goto err;
    }

    // update parent's pte to be read-only COW as well (idempotent if already COW)
    *pte = PA2PTE(pa) | newflags;

    // increment reference count for the shared physical page
    increfpa(pa);
  } else {
    // For read-only or non-user pages, just copy them normally
    if((mem = kalloc()) == 0)
      goto err;
    memmove(mem, (char*)pa, PGSIZE);
    if(mappages(new, i, PGSIZE, (uint64)mem, flags) != 0){
      kfree(mem);
      goto err;
    }
    increfpa((uint64)mem);
  }
}
```

说明（要点）：
- 若父页为可写用户页或已存在 PTE_COW，则设置 `newflags = (flags & ~PTE_W) | PTE_COW`，把该物理页映射到子页表（不复制），并把父页的 PTE 也更新为只读 COW（幂等）。最后调用 `increfpa(pa)` 来记录共享引用。
- 否则（非用户可写页），继续使用原始的复制行为：分配新页、memmove、映射，并 `increfpa` 对新页计数。
- 错误处理（`err`）保留：在失败时通过 `uvmunmap(new, 0, i / PGSIZE, 1)` 清理。

`copyout` 的修改（当内核写到用户空间时处理 COW）：

```c
if(pte == 0 || (*pte & PTE_V) == 0 || (*pte & PTE_U) == 0)
  return -1;

pa0 = PTE2PA(*pte);
flags = PTE_FLAGS(*pte);

// Handle COW pages: check PTE_COW flag
if((flags & PTE_COW) && (flags & PTE_W) == 0){
  // This is a COW page, need to allocate new page
  char *mem = kalloc();
  if(mem == 0)
    return -1;
  memmove(mem, (char*)pa0, PGSIZE);
  decrefpa(pa0);
  increfpa((uint64)mem);
  // Remove COW flag and add write permission
  flags = (flags & ~PTE_COW) | PTE_W;
  *pte = PA2PTE((uint64)mem) | flags;
  pa0 = (uint64)mem;
} else if((flags & PTE_W) == 0){
  // Not COW but not writable either, error
  return -1;
}

n = PGSIZE - (dstva - va0);
if(n > len)
  n = len;
```

说明：
- 如果目标页为 COW 并且当前没有写权限，则在 copyout 路径上执行与 trap 相同的复制逻辑：分配、memmove、decrefpa、increfpa、更新 PTE。这样内核写入用户空间时也会正确处理 COW。
- 若不是 COW 且无写权限，返回错误。

---

## 5) 新增文件：`time.txt`

内容：

```
11
```

该文件看似仅用于记录（非功能性改动）。

---

# 变更影响总结（再强调）

- 功能：在 xv6 内核中实现 COW，减少 fork 时不必要的内存复制，按需在写入时复制页面。
- 安全性：对 MAXVA 越界进行了保护，避免 walk() panic。
- 并发：为 `refcount` 引入 `ref_lock` 来保证多 hart 下的并发安全，但单全局锁可能影响性能。
- 测试：作者声称回归测试全部通过（Score 130/130），建议仍做并发压力测试。

---

# 附：完整新增源码片段（摘自补丁中新增行，便于审阅）

（此处重复上文中各文件新增代码片段，便于拷贝审阅）

- `kernel/riscv.h` 新增宏：

```c
// use RSW (Reserved for Software) bits for COW
#define PTE_COW (1L << 8) // copy-on-write page
```

- `kernel/trap.c` 中新增 extern 声明与 usertrap COW 分支（见上文完整代码块）。

- `kernel/vm.c` 中新增 `refcount`、`ref_lock`、`increfpa`、`decrefpa`、uvmcopy/copyout 修改（见上文完整代码块）。

---

# 建议与后续工作

1. 把本文件作为 PR 描述放到仓库根目录或 `docs/` 中，供代码审阅者参考（已写入仓库根 `PR-afd5d0b-COW.md`）。
2. 在 debug 模式下对 `decrefpa` 添加断言以捕获潜在引用计数不一致问题。
3. 考虑替换全局 `ref_lock` 为更细粒度的锁或使用原子操作以降低争用。
4. 补充自动化测试（多 hart 并发 fork/exit、各种边缘情况）。

---

文件已写入：`/home/steve/xv6/xv6-riscv/PR-afd5d0b-COW.md`

下一步我将：
- 标记 todo 中 `create-markdown-file` 为 completed 并执行 `verify-file-created`（检查文件存在）。

如果你同意，我会继续进行文件存在性校验并把 todo 标记为完成，然后可以选择我帮你提交一个本地 commit 或生成 PR 描述文本。