# COW 代码详解（xv6-riscv）

目的：逐函数和逐组件详细解释基于 commit afd5d0b9... 的写时复制（Copy-On-Write）实现，便于阅读、审阅与维护。

范围：`kernel/vm.c`、`kernel/trap.c` 的关键函数与数据结构，`kernel/riscv.h` 中 PTE 标志的意义，`copyout`/`copyin` 的行为约束，以及 `uvmcopy` 实现要点。

阅读建议：先读 `COW_IMPLEMENTATION.md`（高层说明），然后阅读本文件以获得代码级理解。

---

## 全局约定与契约

1. 物理页与虚拟地址约定
   - 内核使用 direct mapping：内核虚拟地址 KERNBASE 映射到物理地址 0，内核可以通过 `pa` 值直接作为虚地址减去 `KERNBASE` 进行索引。
   - 物理页大小为 `PGSIZE`（4096 bytes）。

2. PTE 位含义（重要位）
   - `PTE_V`：有效
   - `PTE_R/PTE_W/PTE_X`：读/写/执行
   - `PTE_U`：用户可访问
   - `PTE_COW`（软件位）：标记为写时复制页（内核自定义的软件位，比如使用 RSW 位）

3. refcount 数组契约
   - `refcount[idx]` 表示物理页 `pa = KERNBASE + idx * PGSIZE` 的引用次数。
   - 任何对 `refcount` 的读写必须在 `ref_lock` 自旋锁保护下（或通过被保护的 helper 函数）完成。
   - `increfpa(pa)`：增加引用计数（当 pa 合法）。
   - `decrefpa(pa)`：减少引用计数，若降到 0 则释放物理页（调用 `kfree`）。

---

## 文件：kernel/riscv.h（关键宏）
- 定义 `PTE_COW`：使用软件位标记 COW 页面，示例 `#define PTE_COW (1L << 8)`。
- `PA2PTE` / `PTE2PA` / `PTE_FLAGS`：用于 PTE 的位包装/解包操作。

备注：使用软件位意味着硬件忽略该位；内核在页表操作中用它来区分 COW 页面。

---

## 文件：kernel/vm.c

此文件包含大部分内存与页表管理逻辑，COW 改动主要集中于：`uvmcopy`、`uvmfirst`、`uvmalloc`、`uvmunmap`、`increfpa`、`decrefpa`、`copyout`、`walk` 等函数。下面按函数/逻辑块逐一说明。

### 1) 全局变量
- `#define NPHYSPAGES ((PHYSTOP - KERNBASE) / PGSIZE)`：物理页数上限。
- `uint refcount[NPHYSPAGES];`：引用计数数组。
- `struct spinlock ref_lock;`：保护 `refcount` 的自旋锁。

约束：`refcount` 索引通过 `(pa - KERNBASE)/PGSIZE` 计算，必须确保 `pa` 在 `[KERNBASE, PHYSTOP)`。

### 2) kvminit / kvmmake
- 在 `kvminit()` 中调用 `initlock(&ref_lock, "refcount")` 初始化锁。

### 3) walk(pagetable_t pagetable, uint64 va, int alloc)
- 功能：返回指向给定 `va` 的 PTE 的指针（或在需要时分配页表页）。
- 边界检查：`if (va >= MAXVA) panic("walk");` —— 因此调用 `walk()` 前必须确保 `va < MAXVA`。

契约：`walk()` 不做用户地址越界的容错；在处理用户触发的异常（如 page fault）前，应先检测并处理越界情况。

### 4) uvmfirst / uvmalloc
- `uvmfirst()`：为第一个用户页 `kalloc()` 分配物理页，`mappages()` 将其映射到用户虚拟地址 0，并调用 `increfpa((uint64)mem)` 来记录引用计数（为该物理页创建引用）。
- `uvmalloc()`：在扩展用户地址空间时，按页分配，映射并 `increfpa()`。

契约：任何通过 `kalloc()` 新分配的页应在映射后通过 `increfpa()` 让 refcount 反映此持有者。

### 5) mappages / uvmunmap
- `mappages()`：按页建立 PTE，对每个页调用 `walk()`（alloc=1）并设置 `*pte = PA2PTE(pa) | perm | PTE_V`。
- `uvmunmap()`：取消映射并（若 `do_free`）调用 `decrefpa(pa)` 用于物理页释放控制；随后把 `*pte = 0`。

注意：`decrefpa` 会在锁保护下递减计数，并在计数为 0 时调用 `kfree((void*)pa)`。`kfree` 对物理页进行真正的释放。

### 6) uvmcopy(pagetable_t old, pagetable_t new, uint64 sz)

函数职责：fork 时复制父的用户地址空间到子，一般实现为 COW：尽量共享物理页，在写时再复制。

逻辑（已修改后的行为）：
- 对遍历范围的每个页 `i`：
  - 取得父 `pte = walk(old, i, 0)`；若 `PTE_V` 未设置则 panic（或错误处理）。
  - 读取 `pa = PTE2PA(*pte)` 和 `flags = PTE_FLAGS(*pte)`。
  - 如果 `flags & PTE_COW` 已被置（父页已经是 COW），或者 `flags` 包含 `PTE_W` 和 `PTE_U`（可写且用户页），则：
    - 设 `newflags = (flags & ~PTE_W) | PTE_COW`（清写位、置 COW）。
    - 在子页表上以 `mappages(new, i, PGSIZE, pa, newflags)` 把相同 pa 映射过去（共享）。
    - 在父的 pte 上写回 `*pte = PA2PTE(pa) | newflags`（确保父也标为 COW）。
    - 调用 `increfpa(pa)` 增加物理页引用计数。
  - 否则（只读或内核页等）：为子分配新页 `mem = kalloc()`、memmove、`mappages(new, i, PGSIZE, (uint64)mem, flags)`、并 `increfpa((uint64)mem)`。

契约：`uvmcopy()` 保证对于共享的页，父/子都不持写权限，直到其中一方触发写缺页并在缺页处理里复制。

边界条件：
- 当 `mappages()` 或 `kalloc()` 失败时，`uvmcopy` 需要撤销已分配的映射并释放资源（调用 `uvmunmap(new, 0, i/PGSIZE, 1)`）。实现要保证在错误路径上不发生内存泄漏或重复释放。

### 7) increfpa(uint64 pa) / decrefpa(uint64 pa)

职责：维护 `refcount`；控制物理页实际释放时机。

实现要点：
- `increfpa(pa)`：
  - 若 `pa` 不在 `[KERNBASE, PHYSTOP)` 返回；
  - 计算 `idx = (pa - KERNBASE) / PGSIZE`，如果 `idx >= NPHYSPAGES` panic；
  - `acquire(&ref_lock)`，`refcount[idx]++`，`release(&ref_lock)`。
- `decrefpa(pa)`：
  - 同样的边界检查；
  - `acquire(&ref_lock)`，如果 `refcount[idx] > 0` 则 `refcount[idx]--`；若结果为 0，设置标志 `needfree = 1`；`release(&ref_lock)`；若 `needfree` 则 `kfree((void*)pa)`。
  - 否则当 `refcount[idx] == 0` 时，若仍调用 `decrefpa`（异常），实现可能直接 `kfree((void*)pa)` 或记录错误——理想情况下不应发生此类错误。

并发注意：对 `refcount` 的每次操作都必须在 lock 下完成以避免竞态导致早释放或重复释放。

### 8) copyout / copyin / copyinstr

- `copyout(pagetable, dstva, src, len)`：当内核向用户地址写数据时，`copyout()` 做了一系列检查（`va0 < MAXVA`、`pte != 0`、`PTE_V`、`PTE_U`），并读取 `pa0` 与 `flags`。
- 如果检测到 `PTE_COW` 并且写权限没有设置（`(flags & PTE_COW) && (flags & PTE_W) == 0`），`copyout()` 会为该页分配一个新物理页 `mem = kalloc()`，复制原页内容到 `mem`，执行 `decrefpa(pa0)` 与 `increfpa((uint64)mem)`，然后在 PTE 上替换为 `PA2PTE(mem) | (flags & ~PTE_COW) | PTE_W`，并继续写操作。
- 若该页不是 COW 但也不可写，`copyout()` 返回 -1（错误）。

契约：内核写用户内存的路径必须处理 COW 情形，否则会违反共享页语义或导致写时破坏其他进程的内存。

---

## 文件：kernel/trap.c

### 1) usertrap()

此函数处理从用户态进入的 trap，包括 system call、设备中断与异常（page fault 等）。我们关注 store page fault（RISC-V scause == 15）。

处理逻辑（与 COW 相关段）：
- 确认 trap 是来自用户态（`SSTATUS_SPP == 0`）。
- 把 trap 向量切换为内核处理 `kernelvec`。
- 保存 `sepc`。
- 分支处理：syscall、device interrupt 或其他异常。
- 对于 `scause == 15`（store page fault）：
  - 读取 `va = r_stval()`，并计算 `va0 = PGROUNDDOWN(va)`。
  - 重要：先检查 `va0 >= MAXVA` —— 如果越界，则打印诊断并 `setkilled(p)`；返回时会在外面执行 `exit(-1)`。
  - 否则执行 `pte = walk(p->pagetable, va0, 0)`，并验证 `pte`、`PTE_V`、`PTE_U`。
  - 读取 `pa = PTE2PA(*pte)` 与 `flags = PTE_FLAGS(*pte)`。
  - 如果 `PTE_COW` 被置且 `PTE_W` 未设置：
    - 在 `ref_lock` 下读取 `cnt = refcount[idx]`；
    - 若 `cnt > 1`：分配新页 `mem = kalloc()`，复制原页到新页，执行 `decrefpa(pa)` 与 `increfpa((uint64)mem)`，并更新 `*pte = PA2PTE((uint64)mem) | (flags & ~PTE_COW) | PTE_W`，最后 `sfence_vma()`。
    - 若 `cnt == 1`：直接在 PTE 中恢复写权限与清除 `PTE_COW`（无复制），`sfence_vma()`。
    - 否则（计数异常），`setkilled(p)`。
  - 如果不是 COW 情况，则打印并 `setkilled(p)`（非法 page fault）。

重要性：`usertrap()` 的实现必须保证：
- 在对 PTE 进行写修改后调用 `sfence_vma()` 以使 TLB 使更新生效。
- 在 `walk()` 调用前处理越界地址以避免 `walk()` 的 `panic("walk")`。

---

## 不变式（必须保持的条件）

1. 任何映射到物理页的挂起映射必须由 `refcount` 表达（即每次新映射需要 `increfpa`）。
2. 对于 `PTE_COW` 的页，内核空间写操作必须先完成 COW（例如在 `copyout()` 中处理）。
3. `refcount` 的任何变更都在 `ref_lock` 下完成并保证内存可见性。释放时在解锁后执行 `kfree` 以避免长时间持锁。

---

## 常见错误与诊断步骤（快速清单）

1. panic("walk")：
   - 检查 `usertrap()` 是否在 `walk()` 之前验证 `va0 < MAXVA`。
2. lost some free pages / 内存泄漏：
   - 在 `uvmcopy()` 检查是否对原本 COW 的页重复分配；确保对 COW 页使用共享逻辑并 `increfpa(pa)`。
   - 在 `decrefpa` 中临时打印 `idx`/`refcount[idx]` 以追踪释放行为（调试时使用）。
3. copyout/copyin 失败：
   - 在 `copyout` 的 PTE 分支处打印 `flags`，确认 `PTE_COW` 被正确检测并触发 COW 路径。
4. 多核并发错误：
   - 确认 `ref_lock` 已在所有增加/减少/读取 `refcount` 的路径上被使用（包括 `trap.c` 中读取）。

---

## 建议的扩展与硬化

- 性能：把 `refcount` 的全局锁替换为分段锁（例如每 1024 页一把 lock），或者使用原子整型（如果可用且内核支持），以减少在大量并发 fork/exit 时的争用。
- 覆盖测试：添加压力测试脚本，运行多 hart 并发 fork/exit、并监视 `refcount` 整体一致性。
- 文档：在 `riscv.h` 和 `vm.c` 的注释中明确 `PTE_COW` 的语义，以及 `refcount` 的索引和使用规范。

---

如果你愿意，我可以接下来：
- 把此文档整合进仓库的 docs 目录并创建对应的英文版；
- 把实现拆成更清晰的 commits 并创建 PR；
- 实施分段锁优化并跑回归测试。

要我先做哪项？
