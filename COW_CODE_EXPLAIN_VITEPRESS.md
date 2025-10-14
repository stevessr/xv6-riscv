---
home: false
sidebarDepth: 2
---

# 写时复制（COW）代码交叉点评（VitePress 版）

此文档使用 VitePress 的扩展语法（提示/警告块、tabs、代码分组等）来展示 `kernel/vm.c`、`kernel/trap.c` 和 `kernel/riscv.h` 中与写时复制（Copy-On-Write）实现相关的关键代码片段及其交叉点评。

> 注意：为了避免大量复制内核源代码，本文件引用并摘录关键、经过注释的代码片段；它假定源代码在仓库中并与本说明保持同步。

---

## 目录

- [概览](#概览)
- [关键宏与数据结构](#关键宏与数据结构)
- [`vm.c` 逐函数点评](#vmc-逐函数点评)
  - `increfpa` / `decrefpa`
  - `uvmalloc` / `uvmfirst`
  - `uvmcopy`
  - `uvmunmap`
  - `copyout`
- [`trap.c` 相关处理](#trapc-相关处理)
- [并发、不变式与调试清单](#并发不变式与调试清单)
- [附录：如何在本仓库中交叉引用源码](#附录如何在本仓库中交叉引用源码)

---

## 概览

- 目标：在 fork 时尽可能共享父子页表映射（减少内存与复制开销），通过用户写时触发缺页（store fault）来做真正的复制。
- 实现要点：
  - 引入 `PTE_COW` 软件位标记 COW 页面；
  - 用 `refcount[]` 跟踪每个物理页的引用数；
  - fork（`uvmcopy`）时对可写用户页清写位并加 `PTE_COW`；
  - 在内核处理写缺页（`usertrap` 中）时，若页面被共享（refcount>1）就分配新页并复制。

---

## 关键宏与数据结构

::: tip
下面展示在 `kernel/riscv.h` 和 `kernel/vm.c` 中最关键的宏与全局变量。
:::

```c
// kernel/riscv.h
#define PTE_COW (1L << 8) // 软件位

// kernel/vm.c
#define NPHYSPAGES ((PHYSTOP - KERNBASE) / PGSIZE)
uint refcount[NPHYSPAGES];
struct spinlock ref_lock; // 保护 refcount
```

- 注：`PTE_COW` 可用任何未被硬件使用的软件位（例如 RSW 中的一位）实现，但务必在 `riscv.h` 中统一定义。

---

## vm.c 逐函数点评

下面每个小节用两栏布局：左侧为带注释的关键代码片段（缩短版），右侧为逐行点评、调用关系与不变式。VitePress 的 `::: row` 样式可以在主题中渲染为并列列（若主题支持）。若你的主题不支持，可按顺序阅读左右两栏内容。

### increfpa(pa) / decrefpa(pa)

::: row


```c
// 左侧：increfpa (缩略)
```
:::

::: row

```text
右侧点评：
- 内核写用户内存的路径也必须触发 COW，因为内核是 "写者"。
- 先分配再释放的顺序可降低短暂没有任何持有者的窗口。
- 仍要注意并发情况：若另一 CPU 同时处理相同的 COW 页，可能会出现两个副本被同时分配的竞争，从而产生数据一致性风险。refcount 与锁的设计会影响此类竞争的范围与频率。
```

:::
::: row


```text
右侧点评：
- 检查 pa 范围是必须的，索引超界会导致内存损坏（早期 bug 来源）。
- 使用全局自旋锁 `ref_lock` 确保在 SMP 下的原子性。
- 不在锁内调用 `kfree`，因此只做计数递增。若需要查看当前值，可在此处添加调试打印（临时）。
```

:::


```c
// 左侧：decrefpa (缩略)
void
decrefpa(uint64 pa) {
  if (pa < KERNBASE || pa >= PHYSTOP) return;
  uint64 idx = (pa - KERNBASE) / PGSIZE;
  int dofree = 0;
  acquire(&ref_lock);
  if (refcount[idx] > 0) {
    refcount[idx]--;
    if (refcount[idx] == 0) dofree = 1;
  }
  release(&ref_lock);
  if (dofree) kfree((void*)pa);
}
```

:::


::: note
decrefpa 做延时释放（先解锁再 kfree），可减少持锁时间；但要注意在释放前其他 CPU 有可能再次 incref，这时 `dofree` 的 pa 会被 `kfree` 重用导致竞争问题。当前实现要确保 `kfree` 后 pa 的后续 incref 不会错误被视为原先页面的引用（常见做法是加更严格的内存管理或用页描述符表代替直接 pa 索引）。
:::


### uvmfirst / uvmalloc

::: row

```c
// 左侧：uvmfirst (关键段)
void
uvmfirst(pagetable_t pagetable, uchar *src, uint sz) {
  char *mem = kalloc();
  if(mem == 0) return -1;
  memset(mem, 0, PGSIZE);
  mappages(pagetable, 0, PGSIZE, (uint64)mem, PTE_W | PTE_R | PTE_X | PTE_U);
  increfpa((uint64)mem);
  memmove((void*)0, src, sz);
}
```

:::

::: row

```text
右侧点评：
- 新分配页在映射后 `increfpa`：保证 refcount 覆盖所有物理持有者。
- 对新页设置写权限是合理的，因为这是进程第一个拥有者。
- 如果这里漏掉 `increfpa`，后续 `decrefpa`/fork 操作会导致 refcount 不一致，出现提前释放。
```

:::


### uvmcopy

::: row

```c
// 左侧：uvmcopy (核心 COW 逻辑 伪代码)
int uvmcopy(pagetable_t old, pagetable_t new, uint64 sz) {
  for (va = 0; va < sz; va += PGSIZE) {
    pte = walk(old, va, 0);
    pa = PTE2PA(*pte);
    flags = PTE_FLAGS(*pte);
    if ((flags & PTE_COW) || ((flags & PTE_W) && (flags & PTE_U))) {
      newflags = (flags & ~PTE_W) | PTE_COW;
      mappages(new, va, PGSIZE, pa, newflags);
      *pte = PA2PTE(pa) | newflags; // parent now COW too
      increfpa(pa);
    } else {
      // 只读或其他受限，分配新的页给 child
      mem = kalloc();
      memmove(mem, (void*)pa, PGSIZE);
      mappages(new, va, PGSIZE, (uint64)mem, flags);
      increfpa((uint64)mem);
    }
  }
}
```

:::

::: row

```text
右侧点评：
- 关键在于对“可写用户页”执行共享替换：两端均去写位并设置 COW 标志。
- `increfpa` 在共享后调用：若子/父任意一方 later 写触发缺页处理，会使用 refcount 值决定复制或直接恢复写权限。
- 早期一个常见错误：若父页面已经是 COW，此处仍为 child 分配新页（错误的分配逻辑会导致内存使用量暴涨并破坏 COW 语义）。因此实现中必须检测 `PTE_COW` 并仍走共享路径。
- 错误路径：当 `mappages` 或 `kalloc` 失败时需要回滚（`uvmunmap(new,0,va/PGSIZE,1)`），并返回 -1。
```

:::


### uvmunmap（释放行为）

::: row

```c
// 左侧：uvmunmap 的 do_free 分支
if(do_free) {
  uint64 pa = PTE2PA(*pte);
  decrefpa(pa);
}
*pte = 0;
```

:::

::: row

```text
右侧点评：
- 不直接 kfree，而调用 decrefpa，这样多个映射持有同一 pa 时只有最后一个持有者才会触发真实释放。
- 注意要检查 pte 是否有效并且确实指向用户物理页。
```

:::


### copyout / kernel 写用户页场景

::: row

```c
// 左侧：copyout 对 COW 页的处理（精简版）
if ((flags & PTE_COW) && !(flags & PTE_W)) {
  // 分配新页并复制
  char *mem = kalloc();
  memmove(mem, (void*)pa, PGSIZE);
  decrefpa(pa);
  increfpa((uint64)mem);
  *pte = PA2PTE((uint64)mem) | (flags & ~PTE_COW) | PTE_W;
  sfence_vma();
}
// 之后执行常规 copy 到用户空间
```

:::

::: row

```text
右侧点评：
- 内核写用户内存的路径也必须触发 COW，因为内核是 "写者"。
- 先分配再释放的顺序可降低短暂没有任何持有者的窗口。
- 仍要注意并发情况：若另一 CPU 同时处理相同的 COW 页，可能会出现两个副本被同时分配的竞争，从而产生数据一致性风险。refcount 与锁的设计会影响此类竞争的范围与频率。
```

:::

---

## trap.c 相关处理（store page fault）

::: tip
下面是 `usertrap()` 里处理 store page fault（RISC-V scause == 15）的精要逻辑。
:::

```c
// 简化步骤：
uint64 va = r_stval();
uint64 va0 = PGROUNDDOWN(va);
if (va0 >= MAXVA) { setkilled(p); return; }
pte = walk(p->pagetable, va0, 0);
pa = PTE2PA(*pte);
flags = PTE_FLAGS(*pte);
if ((flags & PTE_COW) && !(flags & PTE_W)) {
  acquire(&ref_lock);
  cnt = refcount[(pa - KERNBASE)/PGSIZE];
  release(&ref_lock);
  if (cnt > 1) {
    mem = kalloc();
    memmove(mem, (void*)pa, PGSIZE);
    decrefpa(pa);
    increfpa((uint64)mem);
    *pte = PA2PTE((uint64)mem) | (flags & ~PTE_COW) | PTE_W;
  } else if (cnt == 1) {
    // 只有一个持有者：直接恢复写权限
    *pte = PA2PTE(pa) | (flags & ~PTE_COW) | PTE_W;
  } else {
    setkilled(p);
  }
  sfence_vma();
} else {
  setkilled(p);
}
```

### 交叉点评：`uvmcopy` 与 `usertrap` 的协同

- `uvmcopy` 在 fork 时将 PTE 写位清除并加上 `PTE_COW`，这确保了在任何写事件发生时（无论是用户态的 store，还是内核 copyout 写入）都会触发缺页处理进入 `usertrap`（或 `copyout` 中的 COW 处理路径）。
- `usertrap` 根据 `refcount` 判断是否真的需要复制：`cnt>1` -> 复制并将 PTE 指向新页；`cnt==1` -> 直接恢复写权限（无复制）。
- `sfence_vma()` 在修改 PTE 后必须调用以刷新 TLB，否则 CPU 可能继续使用旧的权限继续执行。

---

## 并发、不变式与调试清单

### 不变式
- 对每个映射的物理页，`refcount` 必须正确反映当前映射数。
- 任何时候 PTE 设置为 `PTE_COW` 时，其 `PTE_W` 必须被清除。
- 任何内核路径写向用户页面前，必须处理可能的 COW（`copyout` 分支或 `usertrap`）。

### 并发注意点
- 当前实现使用单个 `ref_lock` 来保护 `refcount`。在大量并发 fork/exit 时会发生争用。
- `kfree` 在解锁后执行，存在短暂的 pa 被释放并重分配给其他用途的窗口，若某些路径在此期间没有适当的同步可能会出问题。更稳健的实现会使用页描述符对象或跨 CPU 的引用序列化策略。

### 调试清单（优先级排序）
1. panic("walk")：检查是否有针对用户地址的越界访问未提前校验 `MAXVA`。
2. 页释放过早 / double free：在 `decrefpa` 加入打印 `idx` 与 `refcount[idx]`，并在 `kfree` 前后打印 PA。
3. copyout 写入失败：在 `copyout` 的 COW 分支加入日志，确保 `PTE_COW` 被正确识别。
4. 性能问题：在高并发下 `ref_lock` 竞争，可用热点锁分割或原子计数替换。

---

## 附录：如何在本仓库中交叉引用源码

1. 若使用 VitePress，你可以借助内置的 Markdown 链接直接指向源文件：

```md
[查看 vm.c 的 uvmcopy 实现](./kernel/vm.c#L123-L210)
```

> 说明：VitePress/静态站点生成器可能不会自动解析文件行号链接，建议将源代码托管在 GitHub 并使用 GitHub 的行号锚点，或在站点构建脚本中注入源码片段。

---

如果你希望我把此文档转为 `docs/cow.md` 并配置到 VitePress 侧边栏中，或需要我把源码片段直接嵌入（完整函数），告诉我下一步的偏好。我可以：
- 将更多注释化的完整函数体嵌入文档（便于离线阅读）；
- 增加可折叠的“调试示例”代码块并运行一组快速诊断脚本（需运行 QEMU / make 任务）。

