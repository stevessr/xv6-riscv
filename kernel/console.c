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

#define BACKSPACE 0x100 // 定义退格键的ASCII码
#define C(x) ((x) - '@') // 定义Control-x的宏

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
  if (c == BACKSPACE) // 如果是退格键
  {
    // if the user typed backspace, overwrite with a space.
    // 如果用户输入退格，用空格覆盖。
    uartputc_sync('\b'); // 发送退格符
    uartputc_sync(' ');  // 发送空格
    uartputc_sync('\b'); // 再次发送退格符
  }
  else
  {
    uartputc_sync(c); // 发送普通字符
  }
}

struct
{
  struct spinlock lock; // 保护控制台缓冲区的自旋锁

  // input
#define INPUT_BUF_SIZE 128 // 定义输入缓冲区大小
  char buf[INPUT_BUF_SIZE]; // 输入缓冲区
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
  char buf[32]; // 临时缓冲区
  int i = 0; // 循环变量

  while (i < n) // 循环写入
  {
    int nn = sizeof(buf); // 每次最多写入临时缓冲区大小的字节
    if (nn > n - i)
      nn = n - i; // 如果剩余字节数小于临时缓冲区大小，则调整写入字节数
    if (either_copyin(buf, user_src, src + i, nn) == -1) // 从用户空间拷贝数据到临时缓冲区
      break; // 如果拷贝失败，则退出循环
    uartwrite(buf, nn); // 将临时缓冲区的数据写入uart
    i += nn; // 更新已写入字节数
  }

  return i; // 返回实际写入的字节数
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
  uint target; // 目标读取字节数
  int c; // 读取的字符
  char cbuf; // 字符缓冲区

  target = n; // 保存原始请求读取的字节数
  acquire(&cons.lock); // 获取控制台锁
  while (n > 0) // 循环读取
  {
    // wait until interrupt handler has put some
    // 等待中断处理程序将一些输入放入cons.buffer。
    // input into cons.buffer.
    while (cons.r == cons.w) // 如果读写索引相等，说明缓冲区为空
    {
      if (killed(myproc())) // 如果当前进程被杀死
      {
        release(&cons.lock); // 释放控制台锁
        return -1; // 返回错误
      }
      sleep(&cons.r, &cons.lock); // 睡眠，等待输入
    }

    c = cons.buf[cons.r++ % INPUT_BUF_SIZE]; // 从缓冲区读取一个字符

    if (c == C('D')) // 如果是文件结束符 (Control-D)
    { 
      if (n < target) // 如果已经读取了一些字符
      {
        // Save ^D for next time, to make sure
        // 为下一次保存^D，以确保
        // caller gets a 0-byte result.
        // 调用者得到一个0字节的结果。
        cons.r--; // 将读索引减一，下次再读到^D
      }
      break; // 退出循环
    }

    // copy the input byte to the user-space buffer.
    // 将输入字节复制到用户空间缓冲区。
    cbuf = c; // 将字符放入字符缓冲区
    if (either_copyout(user_dst, dst, &cbuf, 1) == -1) // 将字符拷贝到用户空间
      break; // 如果拷贝失败，则退出循环

    dst++; // 目标地址加一
    --n; // 剩余读取字节数减一

    if (c == '\n') // 如果是换行符
    {
      // a whole line has arrived, return to
      // 一整行已经到达，返回到
      // the user-level read().
      // 用户级read()。
      break; // 退出循环
    }
  }
  release(&cons.lock); // 释放控制台锁

  return target - n; // 返回实际读取的字节数
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
  acquire(&cons.lock); // 获取控制台锁

  switch (c)
  {
  case C('P'): // Print process list. 打印进程列表 (Control-P)
    procdump(); // 调用procdump函数打印进程列表
    break;
  case C('U'): // Kill line. 删除行 (Control-U)
    while (cons.e != cons.w && // 当编辑索引不等于写索引，且前一个字符不是换行符时
           cons.buf[(cons.e - 1) % INPUT_BUF_SIZE] != '\n')
    {
      cons.e--; // 编辑索引减一
      consputc(BACKSPACE); // 在控制台输出退格
    }
    break;
  case C('H'): // Backspace 退格 (Control-H)
  case '\x7f': // Delete key 删除键
    if (cons.e != cons.w) // 如果编辑索引不等于写索引
    {
      cons.e--; // 编辑索引减一
      consputc(BACKSPACE); // 在控制台输出退格
    }
    break;
  default:
    if (c != 0 && cons.e - cons.r < INPUT_BUF_SIZE) // 如果字符不为空且缓冲区未满
    {
      c = (c == '\r') ? '\n' : c; // 将回车符转换成换行符

      // echo back to the user.
      // 回显给用户。
      consputc(c); // 将字符回显到控制台

      // store for consumption by consoleread().
      // 存储以供consoleread()使用。
      cons.buf[cons.e++ % INPUT_BUF_SIZE] = c; // 将字符存入缓冲区

      if (c == '\n' || c == C('D') || cons.e - cons.r == INPUT_BUF_SIZE) // 如果是一整行、文件结束符或缓冲区已满
      {
        // wake up consoleread() if a whole line (or end-of-file)
        // 如果一整行（或文件结束）已到达，则唤醒consoleread()。
        // has arrived.
        cons.w = cons.e; // 更新写索引
        wakeup(&cons.r); // 唤醒等待输入的进程
      }
    }
    break;
  }

  release(&cons.lock); // 释放控制台锁
}

void consoleinit(void)
{
  initlock(&cons.lock, "cons"); // 初始化控制台锁

  uartinit(); // 初始化uart

  // connect read and write system calls
  // 将读写系统调用连接到
  // to consoleread and consolewrite.
  // consoleread和consolewrite。
  devsw[CONSOLE].read = consoleread; // 设置控制台的读函数
  devsw[CONSOLE].write = consolewrite; // 设置控制台的写函数
}
