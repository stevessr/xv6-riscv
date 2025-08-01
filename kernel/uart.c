//
// 这是针对 16550a UART (通用异步收发器) 的底层驱动程序。
// UART 是一种串行通信硬件，常用于计算机与外部设备（如终端）的通信。
//

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "proc.h"
#include "defs.h"

// 在RISC-V的 `virt` 机器中，UART设备的控制寄存器被映射到固定的物理内存地址 UART0。
// 内核通过读写这些内存地址来与UART硬件进行交互。这种技术被称为内存映射I/O (Memory-Mapped I/O)。
// Reg宏通过给定的寄存器偏移量(reg)计算出该寄存器的完整物理地址。
// `volatile` 关键字告诉编译器不要对这个地址的读写进行优化（例如缓存到CPU寄存器），
// 因为它指向的是硬件寄存器，其值可能在任何时候被硬件改变。
#define Reg(reg) ((volatile unsigned char *)(UART0 + (reg)))

// 下面是 NS16550A UART 芯片的控制寄存器偏移量定义。
// 详细的寄存器说明可以参考芯片的数据手册，例如: http://byterunner.com/16550.html
//
// RHR (0): 接收保持寄存器 (Receive Holding Register) - 只读。当有数据到达时，从这里读取。
// THR (0): 发送保持寄存器 (Transmit Holding Register) - 只写。当要发送数据时，写入这里。
// RHR 和 THR 共享同一个地址偏移量，通过读/写操作来区分。
#define RHR 0
#define THR 0

// IER (1): 中断使能寄存器 (Interrupt Enable Register)。
// 用于控制 UART 在特定事件发生时是否要向 CPU 请求中断。
#define IER 1
#define IER_RX_ENABLE (1<<0)  // 使能“接收到数据”中断。
#define IER_TX_ENABLE (1<<1)  // 使能“发送保持寄存器为空”中断。

// FCR (2): FIFO 控制寄存器 (FIFO Control Register) - 只写。
// 16550A 芯片内置了 FIFO (先进先出) 缓冲区，可以暂存多个字节，减少中断频率。
#define FCR 2
#define FCR_FIFO_ENABLE (1<<0) // 开启 FIFO 功能。
#define FCR_FIFO_CLEAR (3<<1)  // 清空发送和接收两个方向的 FIFO。

// ISR (2): 中断状态寄存器 (Interrupt Status Register) - 只读。
// 用于查询当前中断的原因（是接收、发送还是其他状态变化）。
#define ISR 2

// LCR (3): 线路控制寄存器 (Line Control Register)。
// 用于配置串行通信的参数，如数据位、停止位、校验位等。
#define LCR 3
#define LCR_EIGHT_BITS (3<<0) // 设置数据位为8位。这是一个常见的配置（8-N-1：8个数据位，无校验，1个停止位）。
#define LCR_BAUD_LATCH (1<<7) // 除数锁存访问位 (DLAB)。当此位置1时，地址为0和1的寄存器变为除数锁存器，用于设置波特率。

// LSR (5): 线路状态寄存器 (Line Status Register) - 只读。
// 提供了当前 UART 线路和数据传输的状态。
#define LSR 5
#define LSR_RX_READY (1<<0)   // 接收保持寄存器 (RHR) 中有数据，可以读取。
#define LSR_TX_IDLE (1<<5)    // 发送保持寄存器 (THR) 为空，并且发送逻辑空闲，可以写入下一个要发送的字节。

// 读写 UART 寄存器的辅助宏。
#define ReadReg(reg) (*(Reg(reg)))
#define WriteReg(reg, v) (*(Reg(reg)) = (v))

// 内核中的软件发送缓冲区。
// 当进程调用 `write` 系统调用向控制台输出时，字符首先被放入这个缓冲区。
// 然后由驱动程序在合适的时候（比如UART硬件空闲时）从这个缓冲区取出并发送。
// 这样做可以避免进程因等待硬件而长时间阻塞。
static struct spinlock uart_tx_lock; // 保护发送缓冲区的自旋锁。
#define UART_TX_BUF_SIZE 32
static char uart_tx_buf[UART_TX_BUF_SIZE];
static uint64 uart_tx_w; // 写指针 (下一个要写入 uart_tx_buf 的位置)。
static uint64 uart_tx_r; // 读指针 (下一个要从 uart_tx_buf 读取的位置)。

extern volatile int panicked; // 来自 printf.c，标记系统是否处于 panic 状态
void uartstart(); // 函数前向声明

//
// 初始化 UART 硬件。
//
void
uartinit(void)
{
  // 1. 禁用所有中断，以防在配置过程中产生干扰。
  WriteReg(IER, 0x00);

  // 2. 设置波特率。这是一个特殊的过程。
  //    首先，设置 LCR 中的 DLAB 位，使地址0和1的寄存器成为波特率除数锁存器。
  WriteReg(LCR, LCR_BAUD_LATCH);
  //    然后，将除数写入这两个寄存器。QEMU `virt` 机器的 UART 时钟频率是 (22.727272 / 2) MHz。
  //    波特率 = 时钟频率 / (16 * 除数)。
  //    对于 38.4K 波特率，除数 = (22.727272e6 / 2) / (16 * 38400) ≈ 18.5。这里使用了一个接近的值3，可能是qemu内部时钟不同。
  //    这里写入的除数值是 3。
  WriteReg(0, 0x03); // 除数的低8位 (LSB)。
  WriteReg(1, 0x00); // 除数的高8位 (MSB)。

  // 3. 恢复 LCR，设置线路参数为 8-N-1 (8个数据位, 无校验, 1个停止位)，并清除 DLAB 位。
  WriteReg(LCR, LCR_EIGHT_BITS);

  // 4. 使能并清空 FIFO。
  WriteReg(FCR, FCR_FIFO_ENABLE | FCR_FIFO_CLEAR);

  // 5. 重新使能接收和发送中断。
  WriteReg(IER, IER_TX_ENABLE | IER_RX_ENABLE);

  // 6. 初始化用于保护发送缓冲区的锁。
  initlock(&uart_tx_lock, "uart");
}

//
// 将一个字符 `c` 添加到发送缓冲区中。
// 如果缓冲区满了，该函数会阻塞调用者（通常是一个进程），直到有空间为止。
// 这个函数设计为在进程上下文（例如 `write` 系统调用）中调用，不能在中断处理程序中调用，因为它会调用 sleep。
//
void
uartputc(int c)
{
  // 外部 printf.c 可能会在 panic 时调用此函数，但那时锁可能已被持有。
  // 在 panic 状态下，我们不关心锁，直接尝试发送。
  // 但正常情况下，必须先获取锁。
  // 外部 printf.c 可能会在 panic 时调用此函数，但那时锁可能已被持有。
  // 在 panic 状态下，我们不关心锁，直接尝试发送。
  // 但正常情况下，必须先获取锁。
  if (panicked) {
    for(;;)
      ;
  }

  acquire(&uart_tx_lock);

  // 如果缓冲区已满 (写指针比读指针超前一个完整的缓冲区大小)，
  // 则必须等待。
  while(uart_tx_w == uart_tx_r + UART_TX_BUF_SIZE){
    // `sleep` 会原子地释放 `uart_tx_lock`，然后让当前进程休眠。
    // 它等待在 `uart_tx_r` 这个“通道”上。当 `uartstart` 从缓冲区取出数据后，
    // 它会调用 `wakeup(&uart_tx_r)` 来唤醒在这里等待的进程。
    // 当被唤醒时，`sleep` 会重新获取锁，然后返回。
    sleep(&uart_tx_r, &uart_tx_lock);
  }

  // 将字符写入循环缓冲区。
  uart_tx_buf[uart_tx_w % UART_TX_BUF_SIZE] = c;
  ++uart_tx_w; // 推进写指针。

  // 尝试启动发送。
  uartstart();

  // 释放锁。
  release(&uart_tx_lock);
}

//
// `uartputc` 的同步版本，不使用中断和软件缓冲区。
// 它会忙等待（自旋），直到 UART 硬件可以接受一个字符为止。
// 主要用于系统早期（调度器和中断还未完全工作时）的 `printf`，以及在 `panic` 状态下打印信息。
//
void
uartputc_sync(int c)
{
  // 禁用中断，防止此函数的操作被中断处理程序干扰。
  // 例如，防止在检查 LSR 和写入 THR 之间发生 UART 中断。
  push_off();

  if (panicked) {
    for(;;)
      ;
  }

  // 忙等待，不断查询线路状态寄存器(LSR)，
  // 直到发送保持寄存器为空 (LSR_TX_IDLE 位被硬件置1)。
  while((ReadReg(LSR) & LSR_TX_IDLE) == 0)
    ;
  // 直接将字符写入硬件的发送保持寄存器 (THR)。
  WriteReg(THR, c);

  // 恢复之前的中断状态。
  pop_off();
}

//
// 核心发送函数。它检查软件发送缓冲区和 UART 硬件状态。
// 如果缓冲区有数据且硬件空闲，它就从缓冲区取出一个字符发送给硬件。
// 这个函数必须在持有 `uart_tx_lock` 的情况下被调用。
//
void
uartstart()
{
  while(1){
    // 如果软件发送缓冲区的读写指针相等，说明缓冲区是空的。
    if(uart_tx_w == uart_tx_r){
      // 缓冲区为空，无事可做，返回。
      return;
    }

    // 查询线路状态寄存器(LSR)，检查硬件的发送保持寄存器是否为空。
    if((ReadReg(LSR) & LSR_TX_IDLE) == 0){
      // 硬件正忙，无法接收新的字符。
      // 我们现在不能发送。硬件在完成当前发送后会触发一个“发送完成”中断。
      // 到时候中断处理程序会再次调用 `uartstart`。所以这里直接返回。
      return;
    }

    // 从循环缓冲区中读取一个字符。
    int c = uart_tx_buf[uart_tx_r % UART_TX_BUF_SIZE];
    ++uart_tx_r; // 推进读指针。

    // 刚刚从缓冲区中取走了一个字符，可能为某个在 `uartputc` 中
    // 因缓冲区满而休眠的进程腾出了空间。
    // `wakeup` 会唤醒所有睡在 `&uart_tx_r` 这个通道上的进程。
    wakeup(&uart_tx_r);

    // 将字符写入硬件的发送保持寄存器，启动物理发送过程。
    WriteReg(THR, c);
  }
}

//
// 从 UART 硬件读取一个字符。
// 这是一个非阻塞的读取。
//
int
uartgetc(void)
{
  // 检查线路状态寄存器(LSR)的“接收数据就绪”(RX_READY)位。
  if(ReadReg(LSR) & 0x01){
    // 如果该位置1，说明硬件的接收保持寄存器(RHR)中有一个字符。
    // 从 RHR 读取该字符并返回。读取操作会自动清除 RX_READY 状态。
    return ReadReg(RHR);
  } else {
    // 如果没有数据，返回 -1。
    return -1;
  }
}

//
// UART 中断处理程序。
// 当 UART 硬件有事件发生时（例如，收到一个字符，或发送完一个字符），
// PLIC (平台级中断控制器) 会将中断路由给 CPU，最终调用此函数。
//
void
uartintr(void)
{
  // 1. 处理接收。
  //    循环调用 uartgetc()，直到它返回-1，以确保读完硬件接收 FIFO 中的所有字符。
  while(1){
    int c = uartgetc();
    if(c == -1)
      break;
    // 将读取到的字符交给上层的 `console` 驱动程序处理。
    // `consoleintr` 会处理行缓冲、特殊字符（如退格、Ctrl+U）和回显等。
    consoleintr(c);
  }

  // 2. 处理发送。
  //    中断发生可能是因为发送保持寄存器变为空闲（TX_IDLE），
  //    这意味着我们可以发送下一个字符了。
  acquire(&uart_tx_lock);
  uartstart(); // 尝试从软件缓冲区发送更多字符。
  release(&uart_tx_lock);
}
