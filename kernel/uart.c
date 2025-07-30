//
// 16550a UART 的底层驱动程序。
//

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "proc.h"
#include "defs.h"

// UART 的控制寄存器被内存映射到物理地址 UART0。
// 这个宏返回其中一个寄存器的地址。
#define Reg(reg) ((volatile unsigned char *)(UART0 + (reg)))

// UART 控制寄存器偏移量定义。
// 有些寄存器在读和写时有不同的含义。
// 参考: http://byterunner.com/16550.html
#define RHR 0                 // 接收保持寄存器 (用于输入字节)
#define THR 0                 // 发送保持寄存器 (用于输出字节)
#define IER 1                 // 中断使能寄存器
#define IER_RX_ENABLE (1<<0)  // 使能接收中断
#define IER_TX_ENABLE (1<<1)  // 使能发送中断
#define FCR 2                 // FIFO 控制寄存器
#define FCR_FIFO_ENABLE (1<<0) // 使能 FIFO
#define FCR_FIFO_CLEAR (3<<1) // 清空发送和接收 FIFO
#define ISR 2                 // 中断状态寄存器
#define LCR 3                 // 线路控制寄存器
#define LCR_EIGHT_BITS (3<<0) // 设置数据位为8位
#define LCR_BAUD_LATCH (1<<7) // 特殊模式，用于设置波特率
#define LSR 5                 // 线路状态寄存器
#define LSR_RX_READY (1<<0)   // RHR 中有数据等待读取
#define LSR_TX_IDLE (1<<5)    // THR 为空，可以接收下一个要发送的字符

// 读写寄存器的宏
#define ReadReg(reg) (*(Reg(reg)))
#define WriteReg(reg, v) (*(Reg(reg)) = (v))

// 发送输出缓冲区
struct spinlock uart_tx_lock; // 保护发送缓冲区的锁
#define UART_TX_BUF_SIZE 32
char uart_tx_buf[UART_TX_BUF_SIZE];
uint64 uart_tx_w; // 写指针, 指向下一个要写入 uart_tx_buf 的位置
uint64 uart_tx_r; // 读指针, 指向下一个要读取 uart_tx_buf 的位置

extern volatile int panicked; // 来自 printf.c 的全局变量，标记系统是否 panic

void uartstart();

void
uartinit(void)
{
  // 禁用所有中断
  WriteReg(IER, 0x00);

  // 进入特殊模式以设置波特率
  WriteReg(LCR, LCR_BAUD_LATCH);

  // 设置波特率为 38.4K。需要写入两个寄存器。
  // LSB (最低有效字节)
  WriteReg(0, 0x03);
  // MSB (最高有效字节)
  WriteReg(1, 0x00);

  // 退出波特率设置模式，
  // 并设置数据位为8位，无校验位。
  WriteReg(LCR, LCR_EIGHT_BITS);

  // 复位并使能 FIFO。
  WriteReg(FCR, FCR_FIFO_ENABLE | FCR_FIFO_CLEAR);

  // 使能发送和接收中断。
  WriteReg(IER, IER_TX_ENABLE | IER_RX_ENABLE);

  // 初始化发送缓冲区的锁
  initlock(&uart_tx_lock, "uart");
}

//
// 将一个字符添加到输出缓冲区，并告诉 UART 开始发送（如果它尚未发送）。
// 如果输出缓冲区已满，则阻塞。
// 因为可能会阻塞，所以不能在中断处理程序中调用；
// 只适用于 write() 等进程上下文调用。
//
void
uartputc(int c)
{
  acquire(&uart_tx_lock);

  // 如果系统已经 panic，则进入死循环，不再发送。
  if(panicked){
    for(;;)
      ;
  }

  // 如果缓冲区已满 (写指针比读指针超前一个缓冲区大小)
  while(uart_tx_w == uart_tx_r + UART_TX_BUF_SIZE){
    // 等待 uartstart() 从缓冲区取出数据，腾出空间。
    // sleep 会原子地释放锁并休眠，等待在 uart_tx_r 这个通道上被唤醒。
    sleep(&uart_tx_r, &uart_tx_lock);
  }
  // 将字符写入缓冲区
  uart_tx_buf[uart_tx_w % UART_TX_BUF_SIZE] = c;
  uart_tx_w += 1; // 移动写指针
  uartstart(); // 尝试启动发送
  release(&uart_tx_lock);
}


//
// uartputc() 的一个替代版本，不使用中断。
// 用于内核 printf() 和回显字符。
// 它会自旋等待，直到 UART 的输出寄存器为空。
//
void
uartputc_sync(int c)
{
  // 关闭中断，防止在操作硬件时被中断干扰
  push_off();

  if(panicked){
    for(;;)
      ;
  }

  // 忙等待，直到 LSR 中的 "发送保持寄存器为空" (TX_IDLE) 标志位被设置。
  while((ReadReg(LSR) & LSR_TX_IDLE) == 0)
    ;
  // 直接将字符写入发送保持寄存器
  WriteReg(THR, c);

  // 恢复之前的中断状态
  pop_off();
}

//
// 如果 UART 空闲，并且发送缓冲区中有等待的字符，则发送它。
// 调用者必须持有 uart_tx_lock。
// 此函数可以被上层（例如 uartputc）和下层（中断处理）调用。
//
void
uartstart()
{
  while(1){
    // 如果读写指针相等，说明缓冲区为空。
    if(uart_tx_w == uart_tx_r){
      // 读一下中断状态寄存器，某些硬件需要这个来清除“发送完成”中断
      ReadReg(ISR);
      return;
    }
    
    // 如果 UART 的发送保持寄存器 (THR) 已满（TX_IDLE 未被设置），
    // 那么我们无法给它更多的数据。
    // 它会在准备好接收新字节时触发一个中断。
    if((ReadReg(LSR) & LSR_TX_IDLE) == 0){
      return;
    }
    
    // 从缓冲区取出一个字符
    int c = uart_tx_buf[uart_tx_r % UART_TX_BUF_SIZE];
    uart_tx_r += 1; // 移动读指针
    
    // uartputc() 可能正在等待缓冲区空间，唤醒它。
    wakeup(&uart_tx_r);
    
    // 将字符写入硬件的发送寄存器
    WriteReg(THR, c);
  }
}

//
// 从 UART 读取一个输入字符。
// 如果没有等待的字符，返回 -1。
//
int
uartgetc(void)
{
  // 检查 LSR 的 RX_READY 位，判断接收保持寄存器中是否有数据。
  if(ReadReg(LSR) & 0x01){
    // 有数据，从 RHR 读取它。
    return ReadReg(RHR);
  } else {
    return -1;
  }
}

//
// 处理 UART 中断。
// 中断触发的原因可能是：有输入到达，或者 UART 准备好接收更多输出，或者两者兼有。
// 由 devintr() 调用。
//
void
uartintr(void)
{
  // 读取并处理所有传入的字符。
  while(1){
    int c = uartgetc();
    if(c == -1)
      break;
    // 将字符传递给控制台层进行处理（例如，行缓冲、回显等）。
    consoleintr(c);
  }

  // 检查是否可以发送缓冲区中的字符。
  acquire(&uart_tx_lock);
  uartstart();
  release(&uart_tx_lock);
}
