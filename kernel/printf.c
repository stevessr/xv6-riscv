//
// 格式化控制台输出 -- printf, panic。
//

#include <stdarg.h>

#include "types.h"
#include "param.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "fs.h"
#include "file.h"
#include "memlayout.h"
#include "riscv.h"
#include "defs.h"
#include "proc.h"

// volatile 确保编译器不会优化掉对 panicked 的读写操作。
// 这是一个全局标志，用于指示系统是否处于 panic 状态。
// 多个核心可能会并发地检查这个标志。
volatile int panicked = 0;

// 用于 printf 的锁，以防止来自不同进程或中断的并发调用导致输出交错。
static struct {
  struct spinlock lock; // 自旋锁，用于保护对控制台的访问
  int locking;          // 一个标志，用于控制是否启用锁机制
} pr;

// 用于数字转换的字符映射表
static char digits[] = "0123456789abcdef";

// 打印一个带符号或无符号的整数。
// xx: 要打印的数字，类型为 long long 以支持长整数。
// base: 转换的进制 (例如, 10 表示十进制, 16 表示十六进制)。
// sign: 是否为有符号数 (1 表示有符号, 0 表示无符号)。
static void
printint(long long xx, int base, int sign)
{
  char buf[20]; // 缓冲区足以存放任何64位整数的字符串表示
  int i;
  unsigned long long x;

  // 如果是有符号数且为负，记录下符号，并将数值转为正数进行处理。
  // 这样可以简化后续的进制转换逻辑。
  if(sign && (sign = (xx < 0)))
    x = -xx;
  else
    x = xx;

  i = 0;
  // 通过循环取余的方式，将数字转换为指定进制的字符串。
  // 例如，对于十进制数 123，第一次循环得到 '3'，第二次得到 '2'，第三次得到 '1'。
  // 字符串是反向生成并存储在缓冲区中的 (e.g., 123 -> "321")。
  do {
    buf[i++] = digits[x % base];
  } while((x /= base) != 0);

  // 如果原始数字是负数，在缓冲区的末尾添加负号。
  if(sign)
    buf[i++] = '-';

  // 将缓冲区中的字符串反向打印到控制台，从而得到正确的顺序。
  // 例如，缓冲区中的 "321-" 会被打印为 "-123"。
  while(--i >= 0)
    consputc(buf[i]);
}

// 打印一个 64 位指针地址 (格式为 0x... )。
// RISC-V 是 64 位架构，所以指针是 64 位。
static void
printptr(uint64 x)
{
  int i;
  consputc('0');
  consputc('x');
  // 打印 16 个十六进制位 (64位 / 4位每十六进制位 = 16)。
  // 每次循环，我们提取 x 的最高4位来获取一个十六进制数字。
  for (i = 0; i < (sizeof(uint64) * 2); i++, x <<= 4)
    consputc(digits[x >> (sizeof(uint64) * 8 - 4)]);
}

// 格式化输出到控制台，是内核的主要打印函数。
// 支持的格式:
//   %d  - 十进制整数
//   %ld - 长整型
//   %lld- 长长整型
//   %u  - 无符号十进制整数
//   %lu - 无符号长整型
//   %llu- 无符号长长整型
//   %x  - 十六进制整数
//   %lx - 十六进制长整型
//   %llx- 十六进制长长整型
//   %p  - 指针
//   %s  - 字符串
//   %%  - 百分号'%'
int
printf(char *fmt, ...)
{
  va_list ap; // 用于处理可变参数的列表
  int i, cx, c0, c1, c2, locking;
  char *s;

  // 检查是否需要加锁。在 panic 或早期启动阶段可能不会加锁。
  locking = pr.locking;
  if(locking)
    acquire(&pr.lock); // 获取自旋锁，确保本次 printf 调用是原子的，不会被其他调用打断。

  va_start(ap, fmt); // 初始化可变参数列表，使其指向第一个可变参数。
  // 遍历格式化字符串 `fmt`
  for(i = 0; (cx = fmt[i] & 0xff) != 0; ++i){
    if(cx != '%'){ // 如果当前字符不是 '%'，则它是一个普通字符
      consputc(cx); // 直接输出到控制台
      continue;
    }

    // 处理 '%' 格式说明符
    ++i; // 移动到 '%' 后面的字符
    c0 = fmt[i+0] & 0xff;
    c1 = c2 = 0; // 预读取'%'后面的字符，以处理多字符格式如 '%llu'
    if(c0) c1 = fmt[i+1] & 0xff;
    if(c1) c2 = fmt[i+2] & 0xff;
    
    // 解析 %d, %ld, %lld (有符号十进制)
    if(c0 == 'd'){
      printint(va_arg(ap, int), 10, 1);
    } else if(c0 == 'l' && c1 == 'd'){
      printint(va_arg(ap, uint64), 10, 1);
      i += 1;
    } else if(c0 == 'l' && c1 == 'l' && c2 == 'd'){
      printint(va_arg(ap, uint64), 10, 1);
      i += 2;
    // 解析 %u, %lu, %llu (无符号十进制)
    } else if(c0 == 'u'){
      printint(va_arg(ap, int), 10, 0);
    } else if(c0 == 'l' && c1 == 'u'){
      printint(va_arg(ap, uint64), 10, 0);
      i += 1;
    } else if(c0 == 'l' && c1 == 'l' && c2 == 'u'){
      printint(va_arg(ap, uint64), 10, 0);
      i += 2;
    // 解析 %x, %lx, %llx (十六进制)
    } else if(c0 == 'x'){
      printint(va_arg(ap, int), 16, 0);
    } else if(c0 == 'l' && c1 == 'x'){
      printint(va_arg(ap, uint64), 16, 0);
      i += 1;
    } else if(c0 == 'l' && c1 == 'l' && c2 == 'x'){
      printint(va_arg(ap, uint64), 16, 0);
      i += 2;
    // 解析 %p (指针)
    } else if(c0 == 'p'){
      printptr(va_arg(ap, uint64));
    // 解析 %s (字符串)
    } else if(c0 == 's'){
      if((s = va_arg(ap, char*)) == 0) // 从可变参数中获取字符串指针
        s = "(null)"; // 如果指针为空，打印 "(null)"
      for(; *s; s++)
        consputc(*s);
    // 解析 %% (打印一个 '%')
    } else if(c0 == '%'){
      consputc('%');
    } else if(c0 == 0){ // 格式字符串以'%'结尾，异常情况
      break;
    } else {
      // 遇到未知的格式说明符，如 "%z"，则打印 "%z" 以引起开发者注意。
      consputc('%');
      consputc(c0);
    }

#if 0
    switch(c){
    case 'd':
      printint(va_arg(ap, int), 10, 1);
      break;
    case 'x':
      printint(va_arg(ap, int), 16, 1);
      break;
    case 'p':
      printptr(va_arg(ap, uint64));
      break;
    case 's':
      if((s = va_arg(ap, char*)) == 0)
        s = "(null)";
      for(; *s; s++)
        consputc(*s);
      break;
    case '%':
      consputc('%');
      break;
    default:
      // Print unknown % sequence to draw attention.
      consputc('%');
      consputc(c);
      break;
    }
#endif
  }
  va_end(ap); // 清理可变参数列表

  if(locking)
    release(&pr.lock); // 释放锁

  return 0;
}

// 当内核遇到无法恢复的严重错误时调用此函数。
// 它会打印一条错误消息，然后停止所有 CPU 的执行。
void
panic(char *s)
{
  pr.locking = 0; // 禁用锁，因为此时系统状态不稳定，或者持有锁的代码可能就是出错的代码。
  printf("内核恐慌: ");
  printf("%s\n", s);
  panicked = 1; // 设置全局 panic 标志。其他 CPU 会在陷阱处理中检查此标志并停止。
  for(;;) // 进入无限循环，停止当前 CPU。
    ;
}

// 初始化 printf 使用的锁。
// 在多核环境中，这对于防止并发的 printf 调用输出混乱至关重要。
void
printfinit(void)
{
  initlock(&pr.lock, "pr"); // 初始化自旋锁
  pr.locking = 1;             // 启用锁机制
}
