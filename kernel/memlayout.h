//-*- coding: utf-8 -*-
// 物理内存布局

// qemu -machine virt 的设置如下,
// 基于 qemu 的 hw/riscv/virt.c:
//
// 00001000 -- 引导 ROM, 由 qemu 提供
// 02000000 -- CLINT (Core Local Interruptor)
// 0C000000 -- PLIC (Platform-Level Interrupt Controller)
// 10000000 -- uart0 (通用异步收发器)
// 10001000 -- virtio 磁盘
// 80000000 -- qemu 的引导 ROM 在这里加载内核,
//             然后跳转到这里。
// 80000000 之后的未使用 RAM。

// 内核这样使用物理内存:
// 80000000 -- entry.S, 然后是内核代码和数据
// end -- 内核页分配区域的开始
// PHYSTOP -- 内核使用的 RAM 结束

// qemu 将 UART 寄存器放在物理内存的这个位置。
#define UART0 0x10000000L
#define UART0_IRQ 10

// virtio mmio 接口
#define VIRTIO0 0x10001000
#define VIRTIO0_IRQ 1

// qemu 将平台级中断控制器 (PLIC) 放在这里。
#define PLIC 0x0c000000L
#define PLIC_PRIORITY (PLIC + 0x0)
#define PLIC_PENDING (PLIC + 0x1000)
#define PLIC_SENABLE(hart) (PLIC + 0x2080 + (hart)*0x100)
#define PLIC_SPRIORITY(hart) (PLIC + 0x201000 + (hart)*0x2000)
#define PLIC_SCLAIM(hart) (PLIC + 0x201004 + (hart)*0x2000)

// 内核期望从物理地址 0x80000000 到 PHYSTOP
// 有可供内核和用户页使用的 RAM。
#define KERNBASE 0x80000000L
#define PHYSTOP (KERNBASE + 128*1024*1024)

// 将 trampoline 页映射到最高地址,
// 在用户和内核空间中都是如此。
#define TRAMPOLINE (MAXVA - PGSIZE)

// 将内核栈映射到 trampoline 下方,
// 每个栈都被无效的保护页包围。
#define KSTACK(p) (TRAMPOLINE - ((p)+1)* 2*PGSIZE)

// 用户内存布局。
// 从地址零开始:
//   代码
//   原始数据和 bss
//   固定大小的栈
//   可扩展的堆
//   ...
//   TRAPFRAME (p->trapframe, 由 trampoline 使用)
//   TRAMPOLINE (与内核中的页面相同)
#define TRAPFRAME (TRAMPOLINE - PGSIZE)