# xv6-riscv: 写时复制 (COW) 实现说明

**日期**：2025-10-09

## 概述
在 xv6-riscv 内核中实现了 fork 的写时复制（Copy-On-Write, COW）。父子在 fork 时共享可写用户页（用 `PTE_COW` 标记并清写权限），通过物理页引用计数延迟复制；只有在实际写发生时才复制物理页。

在实现过程中修复了边界与并发问题，并使用仓库自带测试完成回归（最终通过，Score 130/130）。

本文档包含：变更要点、关键数据结构、主要流程、不变式、遇到的问题与修复、回归测试、复现步骤与后续建议。

## 变更要点（文件）

- `kernel/vm.c`
  - 新增 `refcount[NPHYSPAGES]`；新增 `struct spinlock ref_lock` 并在 `kvminit()` 初始化。
  - `increfpa` / `decrefpa`：在 `ref_lock` 保护下维护引用计数，`decrefpa` 在计数降为 0 时释放物理页。
  - `uvmcopy`：把已标记为 COW 或可写的用户页共享给子进程（清写位、置 `PTE_COW`、`increfpa(pa)`），避免不必要拷贝。
  - `uvmunmap`：释放映射时改为调用 `decrefpa(pa)`（不直接 `kfree`）。

- `kernel/trap.c`
  - 在 `usertrap()` 的 store page fault 分支实现 COW 处理：在 `ref_lock` 下读取 `refcount`，根据计数决定复制或恢复写权限；复制后 `sfence_vma()`。
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
     - `cnt > 1`：分配新页、memmove、`decrefpa(old)`、`increfpa(new)`、更新 PTE 为新页并设写权限，执行 `sfence_vma()`。
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
