//
// Console input and output, to the uart.
// 控制台的输入和输出，到uart。
// Reads are line at a time.
// 一次读取一行。
// Implements special input characters:
// 实现特殊输入字符：
//   newline -- end of line
//   换行符 -- 行尾
//   control-h -- backspace
//   control-h -- 退格
//   control-u -- kill line
//   control-u -- 删除行
//   control-d -- end of file
//   control-d -- 文件结束
//   control-p -- print process list
//   control-p -- 打印进程列表
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
#define C(x) ((x) - '@') // Control-x

//
// send one character to the uart.
// 将一个字符发送到uart。
// called by printf(), and to echo input characters,
// 由printf()调用，并用于回显输入字符，
// but not from write().
// 但不从write()调用。
//
void consputc(int c)
{
  if (c == BACKSPACE)
  {
    // if the user typed backspace, overwrite with a space.
    // 如果用户输入退格，用空格覆盖。
    uartputc_sync('\b');
    uartputc_sync(' ');
    uartputc_sync('\b');
  }
  else
  {
    uartputc_sync(c);
  }
}

struct
{
  struct spinlock lock;

  // input
#define INPUT_BUF_SIZE 128
  char buf[INPUT_BUF_SIZE];
  uint r; // Read index 读取索引
  uint w; // Write index 写入索引
  uint e; // Edit index 编辑索引
} cons;

//
// user write()s to the console go here.
// 用户对控制台的write()调用到这里。
//
int consolewrite(int user_src, uint64 src, int n)
{
  char buf[32];
  int i = 0;

  while (i < n)
  {
    int nn = sizeof(buf);
    if (nn > n - i)
      nn = n - i;
    if (either_copyin(buf, user_src, src + i, nn) == -1)
      break;
    uartwrite(buf, nn);
    i += nn;
  }

  return i;
}

//
// user read()s from the console go here.
// 用户从控制台的read()调用到这里。
// copy (up to) a whole input line to dst.
// 将（最多）一整行输入复制到dst。
// user_dist indicates whether dst is a user
// user_dist指示dst是用户地址还是内核地址。
// or kernel address.
//
int consoleread(int user_dst, uint64 dst, int n)
{
  uint target;
  int c;
  char cbuf;

  target = n;
  acquire(&cons.lock);
  while (n > 0)
  {
    // wait until interrupt handler has put some
    // 等待中断处理程序将一些输入放入cons.buffer。
    // input into cons.buffer.
    while (cons.r == cons.w)
    {
      if (killed(myproc()))
      {
        release(&cons.lock);
        return -1;
      }
      sleep(&cons.r, &cons.lock);
    }

    c = cons.buf[cons.r++ % INPUT_BUF_SIZE];

    if (c == C('D'))
    { // end-of-file 文件结束
      if (n < target)
      {
        // Save ^D for next time, to make sure
        // 为下一次保存^D，以确保
        // caller gets a 0-byte result.
        // 调用者得到一个0字节的结果。
        cons.r--;
      }
      break;
    }

    // copy the input byte to the user-space buffer.
    // 将输入字节复制到用户空间缓冲区。
    cbuf = c;
    if (either_copyout(user_dst, dst, &cbuf, 1) == -1)
      break;

    dst++;
    --n;

    if (c == '\n')
    {
      // a whole line has arrived, return to
      // 一整行已经到达，返回到
      // the user-level read().
      // 用户级read()。
      break;
    }
  }
  release(&cons.lock);

  return target - n;
}

//
// the console input interrupt handler.
// 控制台输入中断处理程序。
// uartintr() calls this for input character.
// uartintr()为输入字符调用此函数。
// do erase/kill processing, append to cons.buf,
// 执行擦除/删除处理，附加到cons.buf，
// wake up consoleread() if a whole line has arrived.
// 如果一整行已到达，则唤醒consoleread()。
//
void consoleintr(int c)
{
  acquire(&cons.lock);

  switch (c)
  {
  case C('P'): // Print process list. 打印进程列表
    procdump();
    break;
  case C('U'): // Kill line. 删除行
    while (cons.e != cons.w &&
           cons.buf[(cons.e - 1) % INPUT_BUF_SIZE] != '\n')
    {
      cons.e--;
      consputc(BACKSPACE);
    }
    break;
  case C('H'): // Backspace 退格
  case '\x7f': // Delete key 删除键
    if (cons.e != cons.w)
    {
      cons.e--;
      consputc(BACKSPACE);
    }
    break;
  default:
    if (c != 0 && cons.e - cons.r < INPUT_BUF_SIZE)
    {
      c = (c == '\r') ? '\n' : c;

      // echo back to the user.
      // 回显给用户。
      consputc(c);

      // store for consumption by consoleread().
      // 存储以供consoleread()使用。
      cons.buf[cons.e++ % INPUT_BUF_SIZE] = c;

      if (c == '\n' || c == C('D') || cons.e - cons.r == INPUT_BUF_SIZE)
      {
        // wake up consoleread() if a whole line (or end-of-file)
        // 如果一整行（或文件结束）已到达，则唤醒consoleread()。
        // has arrived.
        cons.w = cons.e;
        wakeup(&cons.r);
      }
    }
    break;
  }

  release(&cons.lock);
}

void consoleinit(void)
{
  initlock(&cons.lock, "cons");

  uartinit();

  // connect read and write system calls
  // 将读写系统调用连接到
  // to consoleread and consolewrite.
  // consoleread和consolewrite。
  devsw[CONSOLE].read = consoleread;
  devsw[CONSOLE].write = consolewrite;
}