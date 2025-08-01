#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "defs.h"

//
// riscv 平台级中断控制器 (PLIC)。
// PLIC 是一个用于管理来自外部设备（如 UART、磁盘等）的中断的硬件单元。
// 它收集所有设备的中断请求，并根据优先级将它们路由到特定的 CPU 核（hart）进行处理。
//

// 初始化 PLIC
// 这个函数在系统启动时被调用一次，用于设置整个 PLIC 控制器的全局属性。
void
plicinit(void)
{
  // 为每个中断源设置优先级。
  // 在 xv6 中，所有中断的优先级都设置为 1。
  // 优先级为 0 表示禁用该中断。
  // UART0_IRQ 和 VIRTIO0_IRQ 是在 memlayout.h 中定义的设备中断号。
  *(uint32*)(PLIC + UART0_IRQ*4) = 1;
  *(uint32*)(PLIC + VIRTIO0_IRQ*4) = 1;
}

// 为每个 hart 初始化 PLIC
// 这个函数在每个 CPU 核启动时都会被调用，用于配置该核如何处理中断。
void
plicinithart(void)
{
  int hart = cpuid();
  
  // 设置此 hart 想要接收哪些设备的中断。
  // PLIC_SENABLE 宏计算出当前 hart 在 S 模式下的中断使能寄存器的地址。
  // 通过设置相应的位，我们使能了 UART 和 virtio 磁盘的中断。
  *(uint32*)PLIC_SENABLE(hart) = (1 << UART0_IRQ) | (1 << VIRTIO0_IRQ);

  // 设置此 hart 的中断优先级阈值。
  // 只有优先级高于此阈值的中断才会被 PLIC 传递给该 hart。
  // 设置为 0 意味着只要中断的优先级大于 0，它就可以被处理。
  *(uint32*)PLIC_SPRIORITY(hart) = 0;
}

// 从 PLIC 请求下一个待处理的中断。
// 当一个 hart 收到一个外部中断时，它会调用这个函数来确定具体是哪个设备触发了中断。
int
plic_claim(void)
{
  int hart = cpuid();
  // 读取 SCLAIM 寄存器。这个操作是原子的。
  // 读取该寄存器会返回优先级最高的待处理中断的 ID。
  // 同时，这也向 PLIC 表明该中断正在被处理，因此 PLIC 不会再将同级别的这个中断发给其他 hart。
  int irq = *(uint32*)PLIC_SCLAIM(hart);
  return irq;
}

// 通知 PLIC 中断已经处理完毕。
// 在中断服务程序完成后，必须调用此函数。
void
plic_complete(int irq)
{
  int hart = cpuid();
  // 将刚刚处理完的中断 ID 写回到 SCLAIM 寄存器。
  // 这个操作通知 PLIC 该中断已经完成处理，允许 PLIC 未来再次分发这个中断。
  *(uint32*)PLIC_SCLAIM(hart) = irq;
}
