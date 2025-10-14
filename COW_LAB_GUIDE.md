# xv6 COW 实验指导（基于 commit afd5d0b9...）

目标读者：xv6 实验学员、助教或课程维护者。

本指导基于提交 afd5d0b992cbd9054be6609f3231e678a77c01ff（在本仓库的 `cow` 分支上）实现的写时复制（COW）变更。

目的：
- 理解如何在 xv6 中实现 fork 的写时复制（Copy-On-Write）。
- 学会如何修改页表、管理物理页引用计数，以及在 page fault 中完成复制逻辑。
- 能够在本地构建、运行测试并诊断常见问题。

文件清单（重点阅读）：
- `kernel/vm.c` — uvmcopy、uvmunmap、increfpa/decrefpa、copyout/copyin、walk 等实现。
- `kernel/trap.c` — usertrap() 中的写时复制 page-fault 处理。
- `kernel/riscv.h` — `PTE_COW` 的定义。
- `user/usertests.c`、`user/cowtest.c` — 测试用例参考。

实验前准备
- 系统：Linux，已安装 riscv 工具链（riscv64-unknown-elf-gcc 等）和 qemu-system-riscv64。
- 工作目录：仓库根为 `/home/steve/xv6`，实验代码在 `xv6-riscv` 子目录。
- 确保当前分支为变更对应的分支（例如 `cow`），或重置到该提交：

```fish
cd /home/steve/xv6/xv6-riscv
# 如果需要检出指定 commit（可选）
# git checkout afd5d0b992cbd9054be6609f3231e678a77c01ff
```

构建与运行

1. 构建内核：

```fish
make -j4
```

2. 运行完整的 grading 测试（包含 cowtest 和 usertests）：

```fish
./grade-lab-cow
```

这会自动构建（如需）并在 qemu 中运行测试套件。期望结果：所有 cow 相关的测试通过，最终 Score 为 130/130。

关键实现要点（简明版）
- 引入 `PTE_COW`（软件位）来标记写时复制页面。
- 在 fork（`uvmcopy`）时：
  - 对可写且用户可访问的页，清 `PTE_W`、设 `PTE_COW`，将父页和子页映射到同一物理页，调用 `increfpa(pa)`。
  - 对只读或内核映射仍然拷贝分配。
- 在 page fault（store）处理：
  - 如果 PTE 标记 `PTE_COW`，读取 `refcount` 来决定：
    - `ref > 1`：分配新页并复制（decrefpa(old), increfpa(new)），更新 PTE -> 新页并设置写权限。
    - `ref == 1`：直接恢复写权限并清除 `PTE_COW`（无需复制）。
- 使用 `refcount[]` 跟踪物理页引用计数，索引为 `(pa - KERNBASE) / PGSIZE`，数组长度 `NPHYSPAGES`。
- 在多核上为 `refcount` 添加 `ref_lock` 保护避免竞态。

常见错误与调试提示

1. walk() panic（"panic: walk"）
   - 症状：测试或 qemu 报错中看到 `panic("walk")`。
   - 原因：`walk()` 在 va >= MAXVA 时会 panic。`usertrap()` 在处理 fault 前必须先检查 `va0 >= MAXVA` 并 kill 进程，而不是直接调用 `walk()`。
   - 排查：在 `usertrap()` 中打印 `r_stval()`，确认是否为越界地址。

2. "lost some free pages" 或 页丢失 / 重复释放
   - 症状：usertests 报告 "FAILED -- lost some free pages" 或 qemu 崩溃后 free 页数不对。
   - 原因：`uvmcopy()` 对已标记 COW 的页再次分配拷贝，或者 `refcount` 更新存在竞态导致错误释放。
   - 排查：确保 `uvmcopy()` 对 `PTE_COW` 页直接共享并 `increfpa(pa)`；在 `increfpa`/`decrefpa` 中添加边界检查并在多核下使用锁。

3. copyin/copyout 失败
   - 症状：usertests 中 copyin/copyout 测试失败。
   - 原因：内核写用户页（如 `copyout`）未处理 COW；需要在 `copyout()` 中检测 `PTE_COW` 并在写前做 COW（或触发相同的复制逻辑）。
   - 排查：在 `copyout()` 中打印目标 PTE flags，确认 `PTE_COW` 情况并确保 copyout 会为 COW 页分配新页。或者将内核写入改为通过统一的写缺页路径。

额外调试技巧
- 在 `increfpa`/`decrefpa` 中临时打印 `pa`、`idx` 与 `refcount[idx]`（仅用于调试，测试时移除）。
- 在 `usertrap()` 的 page-fault 分支中打印 `sepc`, `stval`, `pte`，观察异常调用栈与缺页地址。
- 使用 `riscv64-unknown-elf-addr2line -e kernel/kernel <addr>` 将 panic 的地址转换到源代码行。

实验报告模板（简短）

填写以下内容以提交实验报告：

- 学生姓名 / 学号：
- 分支 / commit：`afd5d0b992cbd9054be6609f3231e678a77c01ff`
- 实验目标：实现 fork 的写时复制并通过基础测试。
- 所改文件（列表）：
  - `kernel/vm.c`
  - `kernel/trap.c`
  - `kernel/riscv.h`
- 实现要点（简述）：
- 遇到的问题与解决（列举 2-3 个关键问题）：
- 测试结果（贴出 `./grade-lab-cow` 的关键输出或 `xv6.out.usertests`）：
- 个人体会（2-4 行）：

---

我已经把实验指导和报告模板写入 `COW_LAB_GUIDE.md`。下一步我会把 TODO 列表标记为完成并附带提交建议（如果你要我创建 PR 或把改动压成 commit，请指示）。
