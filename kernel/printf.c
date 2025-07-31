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
// 多个核心可能会并发地访问它。
volatile int panicked = 0;

// 用于避免并发的 printf 调用交错输出的锁。
static struct {
  struct spinlock lock;
  int locking; // 是否启用锁
} pr;

static char digits[] = "0123456789abcdef";

// 打印一个带符号或无符号的整数。
// xx: 要打印的数字
// base: 进制 (例如, 10 表示十进制, 16 表示十六进制)
// sign: 是否需要处理符号 (1 表示有符号, 0 表示无符号)
static void
printint(long long xx, int base, int sign)
{
  char buf[20]; // 缓冲区足以存放任何64位整数的字符串表示
  int i;
  unsigned long long x;

  // 如果是有符号数且为负，记录下符号并将数值转为正数处理
  if(sign && (sign = (xx < 0)))
    x = -xx;
  else
    x = xx;

  i = 0;
  // 通过循环取余的方式，将数字转换为指定进制的字符串，并存入缓冲区
  // 注意，字符串是反向生成的 (e.g., 123 -> "321")
  do {
    buf[i++] = digits[x % base];
  } while((x /= base) != 0);

  // 如果是负数，添加负号
  if(sign)
    buf[i++] = '-';

  // 将缓冲区中的字符串反向打印到控制台，得到正确的顺序
  while(--i >= 0)
    consputc(buf[i]);
}

// 打印一个 64 位指针地址 (格式为 0x... )
static void
printptr(uint64 x)
{
  int i;
  consputc('0');
  consputc('x');
  // 打印 16 个十六进制位
  for (i = 0; i < (sizeof(uint64) * 2); i++, x <<= 4)
    consputc(digits[x >> (sizeof(uint64) * 8 - 4)]);
}

// 格式化输出到控制台。
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
  va_list ap; // 用于处理可变参数
  int i, cx, c0, c1, c2, locking;
  char *s;

  locking = pr.locking;
  if(locking)
    acquire(&pr.lock); // 获取锁以保证原子性输出

  va_start(ap, fmt); // 初始化可变参数列表
  // 遍历格式字符串
  for(i = 0; (cx = fmt[i] & 0xff) != 0; i++){
    if(cx != '%'){ // 如果不是格式说明符，直接输出
      consputc(cx);
      continue;
    }
    // 处理格式说明符
    i++;
    c0 = fmt[i+0] & 0xff;
    c1 = c2 = 0;
    if(c0) c1 = fmt[i+1] & 0xff;
    if(c1) c2 = fmt[i+2] & 0xff;
    
    // 解析 %d, %ld, %lld
    if(c0 == 'd'){
      printint(va_arg(ap, int), 10, 1);
    } else if(c0 == 'l' && c1 == 'd'){
      printint(va_arg(ap, uint64), 10, 1);
      i += 1;
    } else if(c0 == 'l' && c1 == 'l' && c2 == 'd'){
      printint(va_arg(ap, uint64), 10, 1);
      i += 2;
    // 解析 %u, %lu, %llu
    } else if(c0 == 'u'){
      printint(va_arg(ap, int), 10, 0);
    } else if(c0 == 'l' && c1 == 'u'){
      printint(va_arg(ap, uint64), 10, 0);
      i += 1;
    } else if(c0 == 'l' && c1 == 'l' && c2 == 'u'){
      printint(va_arg(ap, uint64), 10, 0);
      i += 2;
    // 解析 %x, %lx, %llx
    } else if(c0 == 'x'){
      printint(va_arg(ap, int), 16, 0);
    } else if(c0 == 'l' && c1 == 'x'){
      printint(va_arg(ap, uint64), 16, 0);
      i += 1;
    } else if(c0 == 'l' && c1 == 'l' && c2 == 'x'){
      printint(va_arg(ap, uint64), 16, 0);
      i += 2;
    // 解析 %p
    } else if(c0 == 'p'){
      printptr(va_arg(ap, uint64));
    // 解析 %s
    } else if(c0 == 's'){
      if((s = va_arg(ap, char*)) == 0)
        s = "(null)";
      for(; *s; s++)
        consputc(*s);
    // 解析 %%
    } else if(c0 == '%'){
      consputc('%');
    } else if(c0 == 0){ // 格式字符串以'%'结尾
      break;
    } else {
      // 打印未知的 % 序列以引起注意
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

// 内核发生严重错误时调用。
// 打印错误消息，然后停止所有 CPU。
void
panic(char *s)
{
  pr.locking = 0; // 禁用锁，因为此时系统状态不稳定
  printf("内核恐慌: ");
  printf("%s\n", s);
  panicked = 1; // 设置 panic 标志，以冻结其他 CPU 上的 UART 输出
  for(;;) // 进入无限循环，停止系统
    ;
}

// 初始化 printf 使用的锁
void
printfinit(void)
{
  initlock(&pr.lock, "pr");
  pr.locking = 1;
}
