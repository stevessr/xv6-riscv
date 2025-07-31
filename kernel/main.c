#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "defs.h"

volatile static int started = 0;

// SBI 调用函数
static inline void
sbi_call(uint64 which, uint64 arg0, uint64 arg1, uint64 arg2)
{
  register uint64 a0 asm ("a0") = arg0;
  register uint64 a1 asm ("a1") = arg1;
  register uint64 a2 asm ("a2") = arg2;
  register uint64 a7 asm ("a7") = which;
  asm volatile ("ecall"
                : "+r" (a0)
                : "r" (a1), "r" (a2), "r" (a7)
                : "memory");
}

// 系统关机
void
shutdown(void)
{
  printf("系统正在关机...\n");
  
  // 延迟一下，以便用户看到关机消息
  for(int i = 0; i < 10000000; i++)
    ;
  
  printf("系统已关闭。\n");
  
  // 强制退出系统 - 这将导致 panic 并终止 xv6
  panic("shutdown");
}

// start() 在所有 CPU 上以 supervisor 模式跳转到这里。
void
main()
{
  if(cpuid() == 0){ // 主 CPU (hart 0)
    consoleinit();   // 初始化控制台
    printfinit();    // 初始化 printf
    printf("\n");
    printf("xv6 内核正在启动\n");
    printf("\n");
    kinit();         // 物理页分配器
    kvminit();       // 创建内核页表
    kvminithart();   // 开启分页
    procinit();      // 进程表
    trapinit();      // 陷阱向量
    trapinithart();  // 安装内核陷阱向量
    plicinit();      // 设置中断控制器
    plicinithart();  // 请求 PLIC 提供设备中断
    binit();         // 缓冲区缓存
    iinit();         // inode 表
    fileinit();      // 文件表
    virtio_disk_init(); // 模拟硬盘
    userinit();      // 第一个用户进程
    __sync_synchronize(); // 内存屏障，确保前面的写入对其他 CPU 可见
    started = 1;
  } else { // 其他 CPU (hart)
    while(started == 0) // 等待主 CPU 完成初始化
      ;
    __sync_synchronize();
    printf("hart %d 启动\n", cpuid());
    kvminithart();    // 开启分页
    trapinithart();   // 安装内核陷阱向量
    plicinithart();   // 请求 PLIC 提供设备中断
  }

  scheduler(); // 每个 CPU 进入调度器循环
}
