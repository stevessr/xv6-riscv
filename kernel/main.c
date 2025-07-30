#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "defs.h"

volatile static int started = 0;

// start() 在所有 CPU 上以 supervisor 模式跳转到这里。
void
main()
{
  if(cpuid() == 0){ // 主 CPU (hart 0)
    consoleinit();   // 初始化控制台
    printfinit();    // 初始化 printf
    printf("\n");
    printf("xv6 kernel is booting\n");
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
    printf("hart %d starting\n", cpuid());
    kvminithart();    // 开启分页
    trapinithart();   // 安装内核陷阱向量
    plicinithart();   // 请求 PLIC 提供设备中断
  }

  scheduler(); // 每个 CPU 进入调度器循环
}
