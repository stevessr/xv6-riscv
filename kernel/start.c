//-*- coding: utf-8 -*-
#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "defs.h"

void main();
void timerinit();

// entry.S 需要每个CPU一个栈。
__attribute__((aligned(16))) char stack0[4096 * NCPU];

// entry.S 在 machine 模式下，在 stack0 上跳转到这里。
void start()
{
  // 为了 mret，将 M 先前特权模式设置为 Supervisor。
  unsigned long x = r_mstatus();
  x &= ~MSTATUS_MPP_MASK;
  x |= MSTATUS_MPP_S;
  w_mstatus(x);

  // 为了 mret，将 M 异常程序计数器设置为 main。
  // 需要 gcc -mcmodel=medany
  w_mepc((uint64)main);

  // 暂时禁用分页。
  w_satp(0);

  // 将所有中断和异常委托给 supervisor 模式。
  w_medeleg(0xffff);
  w_mideleg(0xffff);
  w_sie(r_sie() | SIE_SEIE | SIE_STIE);

  // 配置物理内存保护，以授予 supervisor 模式
  // 对所有物理内存的访问权限。
  w_pmpaddr0(0x3fffffffffffffull);
  w_pmpcfg0(0xf);

  // 请求时钟中断。
  timerinit();

  // 将每个CPU的hartid保存在其tp寄存器中，供cpuid()使用。
  int id = r_mhartid();
  w_tp(id);

  // 切换到 supervisor 模式并跳转到 main()。
  asm volatile("mret");
}

// 要求每个hart生成定时器中断。
void timerinit()
{
  // 启用 supervisor 模式的定时器中断。
  w_mie(r_mie() | MIE_STIE);

  // 启用 sstc 扩展（即 stimecmp）。
  w_menvcfg(r_menvcfg() | (1L << 63));

  // 允许 supervisor 使用 stimecmp 和 time。
  w_mcounteren(r_mcounteren() | 2);

  // 请求第一个定时器中断。
  w_stimecmp(r_time() + 1000000);
}