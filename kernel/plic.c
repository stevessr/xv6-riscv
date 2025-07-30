#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "defs.h"

//
// riscv 平台级中断控制器 (PLIC)。
//

// 初始化 PLIC
void
plicinit(void)
{
  // 将所需的 IRQ 优先级设置为非零（否则为禁用）。
  *(uint32*)(PLIC + UART0_IRQ*4) = 1;
  *(uint32*)(PLIC + VIRTIO0_IRQ*4) = 1;
}

// 为每个 hart 初始化 PLIC
void
plicinithart(void)
{
  int hart = cpuid();
  
  // 为此 hart 的 S 模式设置 uart 和 virtio 磁盘的使能位。
  *(uint32*)PLIC_SENABLE(hart) = (1 << UART0_IRQ) | (1 << VIRTIO0_IRQ);

  // 将此 hart 的 S 模式优先级阈值设置为 0。
  // 只有优先级高于阈值的中断才会被处理。
  *(uint32*)PLIC_SPRIORITY(hart) = 0;
}

// 询问 PLIC 我们应该处理哪个中断。
int
plic_claim(void)
{
  int hart = cpuid();
  // 读取 SCLAIM 寄存器来获取下一个待处理的中断 ID。
  int irq = *(uint32*)PLIC_SCLAIM(hart);
  return irq;
}

// 告诉 PLIC 我们已经处理了这个 IRQ。
void
plic_complete(int irq)
{
  int hart = cpuid();
  // 将中断 ID 写回 SCLAIM 寄存器，表示处理完成。
  *(uint32*)PLIC_SCLAIM(hart) = irq;
}
