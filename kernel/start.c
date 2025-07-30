#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "defs.h"

void main();
void timerinit();

// entry.S 需要每个 CPU 一个栈。
// __attribute__ ((aligned (16))) 确保栈是16字节对齐的。
__attribute__ ((aligned (16))) char stack0[4096 * NCPU];

// entry.S 在机器模式下，在 stack0 上跳转到这里。
void
start()
{
  // 设置 M（机器模式）之前的特权级别为 S（监管者模式），为 mret 指令做准备。
  // mret 会将特权级别切换到 mstatus.MPP 字段指定的值。
  unsigned long x = r_mstatus();
  x &= ~MSTATUS_MPP_MASK; // 清除 MPP 字段
  x |= MSTATUS_MPP_S;     // 设置为 Supervisor 模式
  w_mstatus(x);

  // 将 M 模式异常程序计数器（MEPC）设置为 main 函数的地址，为 mret 做准备。
  // mret 会跳转到 MEPC 指定的地址。
  // 这需要 GCC 的 -mcmodel=medany 编译选项。
  w_mepc((uint64)main);

  // 暂时禁用分页。
  // satp 写 0 会关闭虚拟地址转换。
  w_satp(0);

  // 将所有中断和异常委托给监管者模式处理。
  // 这样，当发生中断或异常时，CPU 会直接进入 S 模式，而不是 M 模式。
  w_medeleg(0xffff);
  w_mideleg(0xffff);
  // 开启 S 模式下的外部中断(SEIE)、时钟中断(STIE)和软件中断(SSIE)。
  w_sie(r_sie() | SIE_SEIE | SIE_STIE | SIE_SSIE);

  // 配置物理内存保护（PMP），以允许监管者模式访问所有物理内存。
  // 0x3fffffffffffffull 是一个覆盖所有56位物理地址空间的地址。
  w_pmpaddr0(0x3fffffffffffffull);
  // 0xf 表示允许读(R)、写(W)、执行(X)。
  w_pmpcfg0(0xf);

  // 请求时钟中断。
  timerinit();

  // 将每个CPU的硬件线程ID（hartid）保存在其 tp 寄存器中，供 cpuid() 使用。
  int id = r_mhartid();
  w_tp(id);

  // 切换到监管者模式并跳转到 main()。
  asm volatile("mret");
}

// 请求每个 hart 产生时钟中断。
void
timerinit()
{
  // 启用监管者模式的时钟中断。
  w_mie(r_mie() | MIE_STIE);
  
  // 启用 sstc 扩展（即 stimecmp）。
  // 允许 S 模式直接写 mtimecmp，避免陷入 M 模式。
  w_menvcfg(r_menvcfg() | (1L << 63)); 
  
  // 允许监管者模式使用 stimecmp 和 time 寄存器。
  w_mcounteren(r_mcounteren() | 2);
  
  // 请求第一次时钟中断。
  // time 是当前时间，加上一个间隔后写入 stimecmp。
  // 当 time >= stimecmp 时，会触发一个时钟中断。
  w_stimecmp(r_time() + 1000000);
}
