#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "defs.h"

void main();
void timerinit();

// entry.S 需要为每个 CPU 核（hart）准备一个栈空间。
// xv6 在机器模式（M-mode）下运行，因此需要一个物理地址的栈。
// __attribute__ ((aligned (16))) 确保了栈是 16 字节对齐的，
// 这是 RISC-V ABI（应用程序二进制接口）的要求。
__attribute__ ((aligned (16))) char stack0[4096 * NCPU];

// entry.S 在机器模式（M-mode）下，设置好栈指针后会跳转到这里。
// RISC-V 处理器启动时处于 M-mode，具有最高权限。
// start() 函数的职责是完成从 M-mode 到监管者模式（S-mode）的切换，
// 并为内核的 C 代码执行做好准备。
void
start()
{
  // 1. 设置下一次 `mret` 指令的目标特权级别为监管者模式（S-mode）。
  // `mstatus` 是一个控制状态寄存器。`MSTATUS_MPP_MASK` 用于屏蔽掉
  // MPP（Machine Previous Privilege）字段，然后将其设置为 S-mode。
  // 当 `mret` 执行时，CPU 的特权级别会切换到 MPP 字段指定的值。
  unsigned long x = r_mstatus();
  x &= ~MSTATUS_MPP_MASK; // 清除 MPP 字段
  x |= MSTATUS_MPP_S;     // 设置为 S-mode
  w_mstatus(x);

  // 2. 设置 `mret` 指令的返回地址为 `main` 函数。
  // `mepc`（Machine Exception Program Counter）存放的是异常返回地址。
  // 通过将 `main` 函数的地址写入 `mepc`，`mret` 将会跳转到 `main` 函数执行。
  // 这需要 GCC 的 `-mcmodel=medany` 编译选项，以确保能生成位置无关的代码。
  w_mepc((uint64)main);

  // 3. 禁用分页机制，切换到物理地址模式。
  // `satp`（Supervisor Address Translation and Protection）寄存器控制分页。
  // 写入 0 会关闭虚拟地址到物理地址的转换。
  // 在内核页表建立之前，我们只能使用物理地址。
  w_satp(0);

  // 4. 将所有中断和异常委托给监管者模式（S-mode）处理。
  // `medeleg` (Machine Exception Delegation) 和 `mideleg` (Machine Interrupt Delegation)
  // 寄存器分别控制异常和中断的委托。
  // 将所有位置为 1，意味着 M-mode 将所有类型的异常和中断都直接委托给 S-mode 处理，
  // 这样内核就可以在 S-mode 下统一处理，而无需陷入到 M-mode。
  w_medeleg(0xffff);
  w_mideleg(0xffff);

  // 5. 开启 S-mode 下的外部中断(SEIE)、时钟中断(STIE)和软件中断(SSIE)。
  // `sie` (Supervisor Interrupt Enable) 寄存器控制 S-mode 下的中断使能。
  w_sie(r_sie() | SIE_SEIE | SIE_STIE | SIE_SSIE);

  // 6. 配置物理内存保护（PMP），允许 S-mode 访问所有物理内存。
  // PMP 是 RISC-V 的一个安全特性，可以限制对物理内存的访问。
  // 这里通过设置 PMP 寄存器，将整个物理地址空间配置为对 S-mode 可读、可写、可执行。
  // `pmpaddr0` 设置地址范围，`0x3fffffffffffffull` 覆盖所有 56 位物理地址。
  // `pmpcfg0` 设置权限，`0xf` 表示读(R)、写(W)、执行(X)权限都开放。
  w_pmpaddr0(0x3fffffffffffffull);
  w_pmpcfg0(0xf);

  // 7. 初始化时钟中断。
  // 这将为每个 CPU 核设置一个定时器，以便进行周期性的任务调度。
  timerinit();

  // 8. 将每个 CPU 核的硬件线程ID（hartid）保存在其 `tp` 寄存器中。
  // `tp` 寄存器在 RISC-V 中通常用作线程指针。
  // xv6 利用它来快速获取当前 CPU 的 ID，`cpuid()` 函数会读取这个值。
  int id = r_mhartid();
  w_tp(id);

  // 9. 执行 `mret` 指令，正式切换到监管者模式（S-mode）并跳转到 `main` 函数。
  // 这是从 M-mode 到 S-mode 的关键一步。
  asm volatile("mret");
}

// 为每个 hart 设置时钟中断。
// S-mode 不能直接访问 `mtimecmp`，但可以通过 `stimecmp` 来间接设置。
void
timerinit()
{
  // 1. 在 M-mode 下，使能 S-mode 的时钟中断。
  // `mie`（Machine Interrupt Enable）寄存器控制 M-mode 的中断。
  // MIE_STIE 位控制 S-mode 时钟中断是否能被触发。
  w_mie(r_mie() | MIE_STIE);
  
  // 2. 启用 `sstc` 扩展，允许 S-mode 直接写 `stimecmp` 来触发时钟中断。
  // 这可以避免每次设置时钟都需要陷入到 M-mode，提高了效率。
  w_menvcfg(r_menvcfg() | (1L << 63)); 
  
  // 3. 允许 S-mode 访问 `time` 寄存器。
  // `mcounteren` 控制 S-mode 对性能计数器的访问权限。
  w_mcounteren(r_mcounteren() | 2);
  
  // 4. 设置第一次时钟中断的触发时间。
  // `stimecmp` 是 S-mode 的时钟比较器。当 `time` 寄存器的值
  // 大于等于 `stimecmp` 的值时，就会触发一个时钟中断。
  // 这里将当前时间加上一个固定的间隔，作为下一次中断的时间点。
  w_stimecmp(r_time() + 1000000);
}
