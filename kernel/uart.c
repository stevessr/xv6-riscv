//
// 16550a UART的底层驱动程序。
//

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "proc.h"
#include "defs.h"

// UART控制寄存器被内存映射到地址UART0。
// 这个宏返回其中一个寄存器的地址。
#define Reg(reg) ((volatile unsigned char *)(UART0 + (reg)))

// UART控制寄存器。
// 有些在读和写时有不同的含义。
// 见 http://byterunner.com/16550.html
#define RHR 0                 // 接收保持寄存器（用于输入字节）
#define THR 0                 // 发送保持寄存器（用于输出字节）
#define IER 1                 // 中断使能寄存器
#define IER_RX_ENABLE (1<<0)
#define IER_TX_ENABLE (1<<1)
#define FCR 2                 // FIFO控制寄存器
#define FCR_FIFO_ENABLE (1<<0)
#define FCR_FIFO_CLEAR (3<<1) // 清除两个FIFO的内容
#define ISR 2                 // 中断状态寄存器
#define LCR 3                 // 线路控制寄存器
#define LCR_EIGHT_BITS (3<<0)
#define LCR_BAUD_LATCH (1<<7) // 设置波特率的特殊模式
#define LSR 5                 // 线路状态寄存器
#define LSR_RX_READY (1<<0)   // 输入正在等待从RHR读取
#define LSR_TX_IDLE (1<<5)    // THR可以接受另一个要发送的字符

#define ReadReg(reg) (*(Reg(reg)))
#define WriteReg(reg, v) (*(Reg(reg)) = (v))

// 用于传输。
static struct spinlock tx_lock;
static int tx_busy;           // UART是否正忙于发送？
static int tx_chan;           // &tx_chan是“等待通道”

extern volatile int panicking; // 来自printf.c
extern volatile int panicked; // 来自printf.c

void
uartinit(void)
{
  // 禁用中断。
  WriteReg(IER, 0x00);

  // 设置波特率的特殊模式。
  WriteReg(LCR, LCR_BAUD_LATCH);

  // 38.4K波特率的LSB。
  WriteReg(0, 0x03);

  // 38.4K波特率的MSB。
  WriteReg(1, 0x00);

  // 退出设置波特率模式，
  // 并将字长设置为8位，无奇偶校验。
  WriteReg(LCR, LCR_EIGHT_BITS);

  // 重置并启用FIFO。
  WriteReg(FCR, FCR_FIFO_ENABLE | FCR_FIFO_CLEAR);

  // 启用发送和接收中断。
  WriteReg(IER, IER_TX_ENABLE | IER_RX_ENABLE);

  initlock(&tx_lock, "uart");
}

// 将buf[]传输到uart。如果uart正忙，它会阻塞，
// 所以不能从中断中调用，只能从write()系统调用中调用。
void
uartwrite(char buf[], int n)
{
  acquire(&tx_lock);

  int i = 0;
  while(i < n){ 
    while(tx_busy != 0){
      // 等待UART发送完成中断
      // 将tx_busy设置为0。
      sleep(&tx_chan, &tx_lock);
    }   
      
    WriteReg(THR, buf[i]);
    i += 1;
    tx_busy = 1;
  }

  release(&tx_lock);
}


// 不使用中断向uart写入一个字节，
// 供内核printf()和回显字符使用。
// 它会自旋等待uart的输出寄存器为空。
void
uartputc_sync(int c)
{
  if(panicking == 0)
    push_off();

  if(panicked){
    for(;;)
      ;
  }

  // 等待LSR中的发送保持寄存器为空。
  while((ReadReg(LSR) & LSR_TX_IDLE) == 0)
    ;
  WriteReg(THR, c);

  if(panicking == 0)
    pop_off();
}

// 从UART读取一个输入字符。
// 如果没有等待的字符，则返回-1。
int
uartgetc(void)
{
  if(ReadReg(LSR) & LSR_RX_READY){
    // 输入数据已准备好。
    return ReadReg(RHR);
  } else {
    return -1;
  }
}

// 处理uart中断，该中断因为输入到达，
// 或uart准备好更多输出，或两者兼而有之而引发。
// 从devintr()调用。
void
uartintr(void)
{
  ReadReg(ISR); // 确认中断

  acquire(&tx_lock);
  if(ReadReg(LSR) & LSR_TX_IDLE){
    // UART完成传输；唤醒发送线程。
    tx_busy = 0;
    wakeup(&tx_chan);
  }
  release(&tx_lock);

  // 读取并处理传入的字符。
  while(1){
    int c = uartgetc();
    if(c == -1)
      break;
    consoleintr(c);
  }
}