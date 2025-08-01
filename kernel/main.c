// 包含 xv6 内核所需的基本类型定义，例如 uint、ushort 等。
#include "types.h"
// 包含内核参数的定义，例如最大进程数 (NPROC)、最大文件数 (NOFILE) 等。
#include "param.h"
// 包含内存布局的定义，例如内核基地址 (KERNBASE)、物理内存结束地址 (PHYSTOP) 等。
#include "memlayout.h"
// 包含 RISC-V 架构相关的定义和内联函数，例如用于读写控制寄存器的函数。
#include "riscv.h"
// 包含内核中所有函数的原型声明，充当内核的全局头文件。
#include "defs.h"

// 一个易失性静态整型变量，用于指示内核是否已完成在主 CPU 上的初始化。
// `volatile` 关键字告诉编译器不要对该变量的访问进行优化（例如，缓存到寄存器中），
// 因为它的值可能会被另一个 CPU（硬件线程）并发地修改。
volatile static int started = 0;

// SBI (Supervisor Binary Interface) 调用函数。
// 这是一个内联函数，用于通过 `ecall` 指令从 supervisor 模式陷入到 machine 模式，
// 以便执行由 RISC-V SBI 规范定义的特权操作，例如关闭系统。
// which: SBI 扩展 ID 和功能 ID
// arg0, arg1, arg2: 传递给 SBI 调用的参数
static inline void
sbi_call(uint64 which, uint64 arg0, uint64 arg1, uint64 arg2)
{
  // 将参数加载到指定的寄存器中。根据 RISC-V 调用约定，
  // a0-a2 用于传递参数，a7 用于传递系统调用/SBI 调用号。
  register uint64 a0 asm ("a0") = arg0;
  register uint64 a1 asm ("a1") = arg1;
  register uint64 a2 asm ("a2") = arg2;
  register uint64 a7 asm ("a7") = which;
  
  // `ecall` 指令会触发一个环境调用异常，将控制权从 S-mode 转移到 M-mode。
  // M-mode 下的陷阱处理程序会处理这个调用。
  asm volatile ("ecall"
                : "+r" (a0)  // a0 寄存器既是输入也是输出（返回值）。
                : "r" (a1), "r" (a2), "r" (a7) // a1, a2, a7 是只读输入。
                : "memory"); // "memory" clobber 告诉编译器，此内联汇编可能会读写任何内存位置。
}

// 内核的 C 代码入口点。
// 在 `_entry` (entry.S) 完成了基本的机器模式设置后，
// 所有 CPU 都会以 supervisor 模式跳转到这里执行。
void
main()
{
  // `cpuid()` 返回当前硬件线程（hart）的 ID。
  // hart 0 被指定为主 CPU，负责执行一次性的系统级初始化。
  if(cpuid() == 0){
    consoleinit();   // 初始化控制台设备（UART），以便内核可以打印输出。
    printfinit();    // 初始化内核的 `printf` 函数。
    printf("\n");
    printf("xv6 内核正在启动\n"); // 打印内核启动信息。
    printf("\n");
    kinit();         // 初始化物理内存分配器，管理空闲的物理页。
    kvminit();       // 创建内核的页表，但尚未启用。
    kvminithart();   // 将内核页表的地址加载到 `satp` 寄存器，为当前 hart 开启分页。
    procinit();      // 初始化进程表和进程相关的锁。
    trapinit();      // 设置陷阱处理的入口点（所有 hart 共享）。
    trapinithart();  // 在当前 hart 上安装内核陷阱向量。
    plicinit();      // 初始化 PLIC（平台级中断控制器），用于路由外部设备中断。
    plicinithart();  // 为当前 hart 在 PLIC 中启用中断。
    binit();         // 初始化块设备缓冲区缓存（buffer cache）。
    iinit();         // 初始化 inode 缓存（inode cache）。
    fileinit();      // 初始化全局文件描述符表（file table）。
    virtio_disk_init(); // 初始化 VirtIO 虚拟磁盘设备。
    userinit();      // 创建第一个用户进程 `init`。
    
    // `__sync_synchronize()` 是一个内存屏障。
    // 它确保在此之前的所有内存写入操作都已完成，并且对其他 CPU 可见，
    // 然后才执行 `started = 1`。
    __sync_synchronize();
    started = 1;     // 设置 `started` 标志，通知其他 hart 初始化已完成。
  } else {
    // 其他 hart (非 hart 0) 会在此处自旋等待。
    while(started == 0)
      ; // 等待主 CPU (hart 0) 完成初始化。

    // 内存屏障，确保能正确读取到主 CPU 对 `started` 的写入。
    __sync_synchronize();
    printf("hart %d 启动\n", cpuid());
    
    // 为当前 hart 设置和启用其自己的内核页表和陷阱。
    kvminithart();    // 开启分页。
    trapinithart();   // 安装内核陷阱向量。
    plicinithart();   // 请求 PLIC 提供设备中断。
  }

  // 所有 CPU（包括主 CPU 和其他 CPU）在完成各自的初始化后，
  // 都会调用 `scheduler()` 函数，开始无限循环地调度和运行进程。
  scheduler();
}
