//
// console.c
//
// 控制台输入/输出驱动。
// xv6 将 UART 作为物理控制台设备。本文件实现了针对 UART 的驱动，
// 提供了一个行缓冲的控制台接口。这意味着：
// 1. 用户输入时，字符被收集到一个内部缓冲区中。
// 2. 用户可以通过特殊控制字符（如退格）进行行编辑。
// 3. 只有当用户输入换行符 `\n` 时，整行输入才对上层（如 read 系统调用）可见。
//
//  一些特殊的输入字符：
//  换行符 (newline) -- 行尾
//  control-h -- 退格
//  control-u -- 删除整行
//  control-d -- 文件结束符
//  control-p -- 打印进程列表
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

// 定义一些特殊的控制字符
#define BACKSPACE 0x100      // 退格符的内部表示
#define C(x)  ((x)-'@')  // 计算 Control-x 组合键对应的 ASCII 值的宏

//
// consputc(int c): 将一个字符发送到 UART 显示。
//
// 该函数是底层输出函数，主要被内核的 printf() 和控制台中断处理函数 consoleintr() 调用。
// 它处理了退格字符的特殊显示逻辑。
// 注意：它不应该被 write() 系统调用直接使用，因为 write 的内容是原始数据流。
//
void
consputc(int c)
{
  if(c == BACKSPACE){
    // 为了在屏幕上实现可见的退格效果（光标左移、擦除字符、光标再次左移），
    // 需要向 UART 发送一个 "退格-空格-退格" 的序列。
    uartputc_sync('\b'); 
    uartputc_sync(' '); 
    uartputc_sync('\b');
  } else {
    // 对于其他所有字符，直接通过同步方式发送到 UART。
    // uartputc_sync 会等待 UART 发送完毕，确保字符被输出。
    uartputc_sync(c);
  }
}

//
// 控制台的全局状态结构体 `cons`
//
struct {
  struct spinlock lock; // 自旋锁，用于保护对该结构体成员的并发访问。

  // 输入缓冲区，实现为一个环形缓冲区 (circular buffer)。
#define INPUT_BUF_SIZE 128
  char buf[INPUT_BUF_SIZE];
  
  // 环形缓冲区的三个核心索引：
  // r (read):  读取索引。consoleread() 从 cons.buf[r] 处开始读取已提交的行。
  // w (write): 写入索引。consoleintr() 将用户输入的完整行提交时，会将 w 更新到 e 的位置。
  // e (edit):  编辑索引。consoleintr() 将新接收到的字符写入 cons.buf[e] 处。
  //
  // 索引关系：
  // - [r, w) 范围是已由用户输入完毕（按下回车）但尚未被应用程序读取的数据。
  // - [w, e) 范围是用户当前正在输入和编辑的行。
  uint r;
  uint w;
  uint e;
} cons;

//
// consolewrite(int user_src, uint64 src, int n): 控制台设备的写函数实现。
//
// 当用户进程调用 write(fd, buf, n)，且 fd 指向控制台设备时，
// file_write() 会最终调用此函数。
//
// user_src: 标记源地址 `src` 是在用户空间 (1) 还是内核空间 (0)。
// src:      源数据缓冲区的地址。
// n:        要写入的字节数。
//
int
consolewrite(int user_src, uint64 src, int n)
{
  int i;

  for(i = 0; i < n; i++){
    char c;
    // either_copyin 是一个辅助函数，能安全地从用户空间或内核空间复制数据。
    if(either_copyin(&c, user_src, src+i, 1) == -1)
      break; // 如果复制失败，则中断写入。
      
    // 直接调用 uartputc，而不是 consputc。
    // 因为 write 系统调用应该输出原始字符流，无需处理退格等特殊编辑字符。
    uartputc(c);
  }

  return i; // 返回成功写入的字节数。
}

//
// consoleread(int user_dst, uint64 dst, int n): 控制台设备的读函数实现。
//
// 当用户进程调用 read(fd, buf, n)，且 fd 指向控制台设备时，
// file_read() 会最终调用此函数。
//
// user_dst: 标记目标地址 `dst` 是在用户空间 (1) 还是内核空间 (0)。
// dst:      目标缓冲区的地址。
// n:        请求读取的最大字节数。
//
int
consoleread(int user_dst, uint64 dst, int n)
{
  uint target = n; // 保存原始请求读取的字节数。
  
  acquire(&cons.lock); // 获取锁，保护对 `cons` 结构体的访问。
  while(n > 0){
    // 检查是否有已提交的行可供读取。
    // 当读索引 `r` 等于写索引 `w` 时，表示缓冲区中没有完整的行。
    while(cons.r == cons.w){
      // 如果进程被标记为 "killed"，则不能再继续等待，必须立即返回。
      if(killed(myproc())){
        release(&cons.lock);
        return -1;
      }
      // 如果没有数据可读，进程将进入睡眠状态，等待输入。
      // sleep() 会原子地完成两件事：
      // 1. 释放 `cons.lock` 锁。
      // 2. 让当前进程休眠在 `&cons.r` 这个 "channel" 上。
      // 当被 consoleintr() 唤醒时，它会重新获取 `cons.lock` 锁，然后继续执行。
      sleep(&cons.r, &cons.lock);
    }

    // 从环形缓冲区中读取一个字符。
    int c = cons.buf[cons.r++ % INPUT_BUF_SIZE];

    // 处理文件结束符 (End-of-File, EOF)，由 Control-D 输入。
    if(c == C('D')){
      if(n < target){
        // 如果本次 read() 已经读取了一些字符，那么我们不消耗这个 C-D。
        // 将读指针 `r` 回退，把 C-D 留给下一次 read() 调用。
        // 这样，本次调用返回已读数据，下次调用将立即读到 C-D 并返回 0。
        cons.r--;
      }
      break; // 结束读取循环。
    }

    // 将读取到的字符复制到目标缓冲区（用户空间或内核空间）。
    char cbuf = c;
    if(either_copyout(user_dst, dst, &cbuf, 1) == -1)
      break; // 如果复制失败，中断读取。

    dst++; // 目标地址后移。
    --n;   // 剩余要读取的字节数减一。

    // 如果读到了换行符，说明一行已经读取完毕，可以返回了。
    // 这是行缓冲控制台的核心行为。
    if(c == '\n'){
      break;
    }
  }
  release(&cons.lock); // 释放锁。

  return target - n; // 返回实际读取的字节数。
}

//
// consoleintr(int c): 控制台中断处理程序。
//
// 每当 UART 硬件接收到一个字符并触发中断时，底层的 `uartintr()` 就会调用此函数，
// 并将接收到的字符 `c` 作为参数传入。
//
void
consoleintr(int c)
{
  acquire(&cons.lock); // 获取锁，以安全地修改 `cons` 状态。

  switch(c){
  case C('P'):  // Control-P: 打印进程列表，用于调试。
    procdump();
    break;

  case C('U'):  // Control-U: 删除当前正在编辑的整行。
    // 将编辑索引 `e` 回退到写索引 `w`，即丢弃当前行所有字符。
    // 同时在屏幕上输出相应数量的退格符以更新显示。
    while(cons.e != cons.w &&
          cons.buf[(cons.e-1) % INPUT_BUF_SIZE] != '\n'){
      cons.e--;
      consputc(BACKSPACE);
    }
    break;

  case C('H'): // Control-H: 退格键
  case '\x7f': // DEL 键 (在很多终端下等同于退格)
    // 只有在当前行有字符时（e > w），退格才有效。
    if(cons.e != cons.w){
      cons.e--; // 编辑索引回退一个字符。
      consputc(BACKSPACE); // 在屏幕上显示退格效果。
    }
    break;

  default:
    // 处理普通字符输入。
    // 检查字符是否有效（不为0），以及缓冲区是否已满。
    // (cons.e - cons.r) 是缓冲区中已存在的字符总数（包括已提交和正在编辑的）。
    if(c != 0 && cons.e-cons.r < INPUT_BUF_SIZE){
      // 标准化行尾：将回车符 '\r' 统一转换成换行符 '\n'。
      c = (c == '\r') ? '\n' : c;

      // 回显：将用户输入的字符显示在屏幕上。
      consputc(c);

      // 将字符存入环形缓冲区，并前移编辑索引 `e`。
      cons.buf[cons.e++ % INPUT_BUF_SIZE] = c;

      // 检查是否一行输入结束。
      // 结束条件：换行符、EOF(C-D) 或缓冲区满。
      if(c == '\n' || c == C('D') || cons.e == cons.r+INPUT_BUF_SIZE){
        // "提交" 这行数据：将写索引 `w` 更新为编辑索引 `e`。
        // 这使得 [r, w) 范围内的所有数据对 consoleread() 可见。
        cons.w = cons.e;
        
        // 唤醒可能在 consoleread() 中因等待数据而休眠的进程。
        // wakeup() 会唤醒所有休眠在 `&cons.r` 这个 channel 上的进程。
        wakeup(&cons.r);
      }
    }
    break;
  }
  
  release(&cons.lock); // 释放锁。
}

//
// consoleinit(void): 控制台设备初始化函数。
//
void
consoleinit(void)
{
  initlock(&cons.lock, "cons"); // 初始化控制台的自旋锁。

  uartinit(); // 初始化物理 UART 设备。

  // 将本驱动的读/写函数注册到全局设备开关表 `devsw` 中。
  // 这样，对设备号为 CONSOLE 的文件进行操作时，内核就会调用这里的
  // consoleread 和 consolewrite 函数。
  devsw[CONSOLE].read = consoleread;
  devsw[CONSOLE].write = consolewrite;
}
