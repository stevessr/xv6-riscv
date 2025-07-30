//
// 控制台输入输出，目标为 uart。
// 每次读取一行。
// 实现了一些特殊的输入字符：
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

#define BACKSPACE 0x100
#define C(x)  ((x)-'@')  // Control-x

//
// 发送一个字符到 uart。
// 被 printf() 调用，用于回显输入字符，
// 但不被 write() 调用。
//
void
consputc(int c)
{
  if(c == BACKSPACE){
    // 如果用户输入退格，用空格覆盖，然后光标退回。
    uartputc_sync('\b'); uartputc_sync(' '); uartputc_sync('\b');
  } else {
    uartputc_sync(c);
  }
}

// 控制台相关的全局数据结构
struct {
  struct spinlock lock; // 锁，保护 cons 结构体
  
  // 输入缓冲区
#define INPUT_BUF_SIZE 128
  char buf[INPUT_BUF_SIZE]; // 缓冲区
  uint r;  // 读取索引 (Read index)
  uint w;  // 写入索引 (Write index)
  uint e;  // 编辑索引 (Edit index)
} cons;

//
// 用户对控制台的 write() 调用最终会到这里。
//
int
consolewrite(int user_src, uint64 src, int n)
{
  int i;

  for(i = 0; i < n; i++){
    char c;
    // 从用户空间或内核空间拷贝数据
    if(either_copyin(&c, user_src, src+i, 1) == -1)
      break;
    uartputc(c); // 将字符输出到 uart
  }

  return i;
}

//
// 用户从控制台的 read() 调用最终会到这里。
// 复制（最多）一整行输入到目标地址 dst。
// user_dst 表示 dst 是用户地址还是内核地址。
//
int
consoleread(int user_dst, uint64 dst, int n)
{
  uint target;
  int c;
  char cbuf;

  target = n;
  acquire(&cons.lock); // 获取锁
  while(n > 0){
    // 等待中断处理程序将一些输入放入 cons.buf。
    while(cons.r == cons.w){ // 当读索引等于写索引时，缓冲区为空
      if(killed(myproc())){ // 如果当前进程被杀死
        release(&cons.lock);
        return -1;
      }
      // 睡眠，等待输入
      sleep(&cons.r, &cons.lock);
    }

    // 从缓冲区读取一个字符
    c = cons.buf[cons.r++ % INPUT_BUF_SIZE];

    if(c == C('D')){  // 文件结束符 (Control-D)
      if(n < target){
        // 为下次保留 ^D，以确保
        // 调用者得到一个 0 字节的结果。
        cons.r--;
      }
      break;
    }

    // 将输入字节复制到用户空间缓冲区。
    cbuf = c;
    if(either_copyout(user_dst, dst, &cbuf, 1) == -1)
      break;

    dst++;
    --n;

    if(c == '\n'){
      // 一整行已经到达，返回到
      // 用户态的 read()。
      break;
    }
  }
  release(&cons.lock); // 释放锁

  return target - n; // 返回读取的字节数
}

//
// 控制台输入中断处理程序。
// uartintr() 为每个输入字符调用此函数。
// 处理删除/行删除，追加到 cons.buf，
// 如果一整行已到达，则唤醒 consoleread()。
//
void
consoleintr(int c)
{
  acquire(&cons.lock); // 获取锁

  switch(c){
  case C('P'):  // 打印进程列表 (Control-P)
    procdump();
    break;
  case C('U'):  // 删除整行 (Control-U)
    while(cons.e != cons.w &&
          cons.buf[(cons.e-1) % INPUT_BUF_SIZE] != '\n'){
      cons.e--;
      consputc(BACKSPACE);
    }
    break;
  case C('H'): // 退格 (Control-H)
  case '\x7f': // 删除键
    if(cons.e != cons.w){
      cons.e--;
      consputc(BACKSPACE);
    }
    break;
  default:
    // 如果字符有效且缓冲区未满
    if(c != 0 && cons.e-cons.r < INPUT_BUF_SIZE){
      c = (c == '\r') ? '\n' : c; // 将回车符转换成换行符

      // 回显给用户
      consputc(c);

      // 存储以供 consoleread() 使用
      cons.buf[cons.e++ % INPUT_BUF_SIZE] = c;

      if(c == '\n' || c == C('D') || cons.e-cons.r == INPUT_BUF_SIZE){
        // 如果一整行（或文件结束符）已到达，
        // 或者缓冲区已满，则唤醒 consoleread()。
        cons.w = cons.e;
        wakeup(&cons.r);
      }
    }
    break;
  }
  
  release(&cons.lock); // 释放锁
}

void
consoleinit(void)
{
  initlock(&cons.lock, "cons"); // 初始化锁

  uartinit(); // 初始化 uart

  // 将读写系统调用连接到
  // consoleread 和 consolewrite。
  devsw[CONSOLE].read = consoleread;
  devsw[CONSOLE].write = consolewrite;
}
