// xv6 在 QEMU 'virt' 机器上的物理内存布局
//
// 本文件定义了硬件物理地址和内核虚拟地址空间的布局。
// 这些地址是针对 QEMU 的 `virt` RISC-V 虚拟化平台设定的。
//
// 硬件设备物理地址映射:
// (CPU 通过读写这些特殊的物理地址来与硬件设备交互，即 Memory-Mapped I/O)
// 0x00001000 -- QEMU 内置的引导 ROM (Boot ROM)
// 0x02000000 -- CLINT (Core-Level Interruptor)，核心本地中断器，负责提供计时器和核间中断。
// 0x0c000000 -- PLIC (Platform-Level Interrupt Controller)，平台级中断控制器，负责处理外部设备中断。
// 0x10000000 -- UART0 (Universal Asynchronous Receiver/Transmitter)，串口设备，用于控制台输入输出。
// 0x10001000 -- Virtio 磁盘设备接口。
//
// 内核物理内存使用:
// 0x80000000 -- 此处是物理内存(DRAM)的起始地址。QEMU 的引导流程会把内核加载到这里。
//             内核的入口点 `entry.S` 就从这里开始执行，其后紧跟着内核的代码和数据段。
// `end`      -- 由链接器脚本定义的符号，表示内核代码和数据段的末尾。
//             从 `end` 到 `PHYSTOP` 的物理内存由页分配器 `kalloc.c` 管理。
// PHYSTOP    -- 定义了可供内核使用的物理内存的上限。

// UART0 (串口) 的物理基地址和中断号
#define UART0 0x10000000L
#define UART0_IRQ 10

// Virtio 磁盘设备的内存映射 I/O 地址和中断号
#define VIRTIO0 0x10001000
#define VIRTIO0_IRQ 1

// CLINT (核心本地中断器) 寄存器物理地址
#define CLINT 0x2000000L
#define CLINT_MTIMECMP(hartid) (CLINT + 0x4000 + 8*(hartid))
#define CLINT_MTIME (CLINT + 0xBFF8) // 存储启动后的时钟周期数

// PLIC (平台级中断控制器) 的寄存器物理地址
// PLIC 负责管理来自外部设备（如 UART, Virtio）的中断请求。
#define PLIC 0x0c000000L
#define PLIC_PRIORITY (PLIC + 0x0)          // 中断优先级寄存器
#define PLIC_PENDING (PLIC + 0x1000)        // 挂起位寄存器，显示哪个中断正在等待处理
#define PLIC_MENABLE(hart) (PLIC + 0x2000 + (hart)*0x100)      // Machine模式下，每个核心的中断使能寄存器
#define PLIC_SENABLE(hart) (PLIC + 0x2080 + (hart)*0x100)      // Supervisor模式下，每个核心的中断使能寄存器
#define PLIC_MPRIORITY(hart) (PLIC + 0x200000 + (hart)*0x2000) // Machine模式下，每个核心的中断优先级阈值
#define PLIC_SPRIORITY(hart) (PLIC + 0x201000 + (hart)*0x2000) // Supervisor模式下，每个核心的中断优先级阈值
#define PLIC_MCLAIM(hart) (PLIC + 0x200004 + (hart)*0x2000)    // Machine模式下，每个核心的中断认领/完成寄存器
#define PLIC_SCLAIM(hart) (PLIC + 0x201004 + (hart)*0x2000)    // Supervisor模式下，每个核心的中断认领/完成寄存器

// 内核地址空间布局
//
// KERNBASE 定义了内核的基地址。内核的虚拟地址从 KERNBASE 开始，
// 映射到相同的物理地址，直到 PHYSTOP。
// (va = pa + KERNBASE - phys_base)
// 这种直接映射包括了内核的代码、数据、以及直到 PHYSTOP 的所有物理内存。
#define KERNBASE 0x80000000L
// PHYSTOP 定义了可供使用的物理内存的上限。这里设置为 KERNBASE + 128MB。
// 任何高于此地址的物理内存都不会被内核使用。
#define PHYSTOP (KERNBASE + 128*1024*1024)

// --- 内核虚拟地址空间的高地址区域 ---
// MAXVA 是 RISC-V Sv39 分页模式下能表示的最大虚拟地址 (2^38 - 1)。

// TRAMPOLINE 页被映射在虚拟地址空间的顶端。
// 这个页在所有进程的页表中都存在，并且映射到相同的物理页。
// 它包含了从用户态切换到内核态（uservec）以及从内核态返回用户态（userret）的代码。
// 因为 trap 发生时硬件不改变页表，所以这个页必须在用户和内核页表中都可用。
#define TRAMPOLINE (MAXVA - PGSIZE)

// 内核栈 (Kernel Stack)
// 每个进程都有一个自己的内核栈，用于在内核态执行时存放函数调用帧和保存的寄存器。
// KSTACK(p) 计算第 p 个进程的内核栈的顶部虚拟地址。
// 内核栈位于 TRAMPOLINE 页的下方。
// 每个栈之间有一个未映射的保护页(guard page)，用于防止栈溢出破坏相邻栈的数据。
// 所以每个进程的内核栈区域大小为 2*PGSIZE (一个栈页 + 一个保护页)。
#define KSTACK(p) (TRAMPOLINE - ((p)+1)* 2*PGSIZE)

// --- 用户进程虚拟地址空间布局 ---
//
// 0x0 (低地址)
//   ... 文本段 (代码)
//   ... 只读数据段
//   ... 数据段 和 BSS 段
//   ... 堆 (heap)，通过 sbrk 向上增长
//   ... (unused)
//   ... 用户栈 (一个页)
//   ... (unused)
// TRAPFRAME (p->trapframe)
// TRAMPOLINE
// MAXVA (高地址)

// TRAPFRAME 页位于 TRAMPOLINE 页的正下方。
// 当发生系统调用或中断时，内核需要保存用户进程的寄存器状态。
// 这些寄存器被保存在当前进程的 `trapframe` 结构体中，
// 而这个结构体所在的物理页就被映射到这个固定的 TRAPFRAME 虚拟地址上。
#define TRAPFRAME (TRAMPOLINE - PGSIZE)
