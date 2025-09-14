#include "types.h"     // 包含类型定义
#include "param.h"     // 包含系统参数
#include "memlayout.h" // 包含内存布局信息
#include "riscv.h"     // 包含RISC-V相关的定义
#include "defs.h"      // 包含内核函数和变量的声明

volatile static int started = 0; // 一个易失性静态变量，用于标记内核是否已经启动

// start() 函数在所有CPU上以supervisor模式跳转到这里。
void main()
{
  if (cpuid() == 0)
  {                // 如果是CPU 0
    consoleinit(); // 初始化控制台
    printfinit();  // 初始化printf函数
    printf("\n");
    printf("xv6 kernel is booting\n"); // 打印内核启动信息
    printf("\n");
    kinit();              // 初始化物理页分配器
    kvminit();            // 创建内核页表
    kvminithart();        // 开启分页
    procinit();           // 初始化进程表
    trapinit();           // 初始化中断向量
    trapinithart();       // 安装内核中断向量
    plicinit();           // 设置中断控制器
    plicinithart();       // 请求PLIC设备中断
    binit();              // 初始化缓冲区缓存
    iinit();              // 初始化inode表
    fileinit();           // 初始化文件表
    virtio_disk_init();   // 初始化模拟硬盘
    userinit();           // 创建第一个用户进程
    __sync_synchronize(); // 内存屏障，确保所有写操作都已完成
    started = 1;          // 设置started标志为1
  }
  else
  { // 如果不是CPU 0
    while (started == 0)
      ;
    __sync_synchronize();                  // 内存屏障，确保读取到最新的started值
    printf("hart %d starting\n", cpuid()); // 打印其他CPU的启动信息
    kvminithart();                         // 开启分页
    trapinithart();                        // 安装内核中断向量
    plicinithart();                        // 请求PLIC设备中断
  }

  scheduler(); // 调用调度器，开始进程调度
}