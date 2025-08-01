#include "types.h"
#include "riscv.h"
#include "defs.h"

// shutdown() - 关闭系统
//
// 该函数用于安全地关闭在 QEMU 中运行的 xv6 系统。
// 它首先尝试使用 QEMU 的特定关机机制，如果失败，则进入一个无限循环，
// 使 CPU 处于低功耗状态，从而有效地停止系统。
void
shutdown(void)
{
  
  printf("系统正在关机...\n");
  
  // 使用 QEMU 的调试设备来触发关机。
  // 向物理地址 0x100000 写入值 0x5555（或者其他一些值，除了0x3333之外）
  // 是 QEMU virt 虚拟化平台约定的关机命令。
  // 这是一种模拟物理电源按钮按下的方式。
  // 更多信息请参考 QEMU 文档中关于 'sifive_test' 设备的部分。
  #define QEMU_POWEROFF_ADDR ((volatile uint32 *)0x100000)
  #define QEMU_POWEROFF_VALUE 0x5555
  
  // 向 QEMU 关机设备地址写入关机指令值。
  *QEMU_POWEROFF_ADDR = QEMU_POWEROFF_VALUE;
  
  // 如果上述关机机制由于某种原因失败，
  // 我们将进入一个无限循环，作为最后的保障措施。
  // 这可以防止内核在关机失败后继续执行未定义行为。
  for(;;) {
    // 'wfi' (Wait For Interrupt) 是一条 RISC-V 指令，
    // 它会使当前 hart（硬件线程或 CPU核心）暂停执行，
    // 直到它收到一个中断请求。
    // 在这里，由于中断在关机流程中通常是禁用的，
    // 'wfi' 会使 CPU 进入一个深度睡眠的低功耗状态，并且不会被唤醒。
    // 这有效地停止了 CPU 的活动。
    asm volatile("wfi");
  }
}