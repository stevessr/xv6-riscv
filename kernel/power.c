#include "types.h"
#include "defs.h"

// 系统关机
void
shutdown(void)
{
  
  printf("系统正在关机...\n");
  
  // 使用 QEMU 关机设备
  // 向地址 0x100000 写入 0x5555 会导致 QEMU 关机
  // 这是 QEMU virt 机器实现的一部分
  #define QEMU_POWEROFF_ADDR ((volatile uint32 *)0x100000)
  #define QEMU_POWEROFF_VALUE 0x5555
  
  // 向 QEMU 关机设备写入关机值
  *QEMU_POWEROFF_ADDR = QEMU_POWEROFF_VALUE;
  
  printf("系统关机失败，进入低功耗模式...\n");
  
  // 最后的手段：使用无限循环和 wfi 指令使 CPU 进入低功耗状态
  for(;;) {
    asm volatile("wfi");  // wait for interrupt - 使 CPU 进入低功耗状态
  }
}