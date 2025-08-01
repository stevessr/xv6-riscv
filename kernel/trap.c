#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "proc.h"
#include "defs.h"

// 全局时钟滴答计数器，用于记录时钟中断发生的次数。
// 这个变量被 `sleep` 系统调用等使用，以实现定时功能。
struct spinlock tickslock;
uint ticks;

// 从汇编文件 trampoline.S 中导入的外部符号地址。
// trampoline: 跳板代码的起始地址，用于用户态和内核态之间的切换。
// uservec: 用户陷阱处理程序的入口点。
// userret: 从内核陷阱返回用户空间的入口点。
extern char trampoline[], uservec[], userret[];

// 在 kernelvec.S 中定义的内核陷阱向量。
// 当在内核态发生中断或异常时，CPU会跳转到这个地址。
void kernelvec();

// 声明外部设备中断处理函数 devintr()。
extern int devintr();

void
trapinit(void)
{
  // 初始化用于保护全局变量 ticks 的自旋锁。
  initlock(&tickslock, "time");
}

// 为每个 CPU核心 (hart) 设置内核态的陷阱处理程序。
void
trapinithart(void)
{
  // 将 supervisor trap vector (stvec) 寄存器设置为 kernelvec 的地址。
  // 这意味着当 CPU 处于内核态（supervisor mode）时，发生的所有中断或异常
  // 都会将程序计数器（PC）设置为 kernelvec 的值，从而开始执行内核陷阱处理流程。
  w_stvec((uint64)kernelvec);
}

//
// 处理来自用户空间的陷阱（中断、异常或系统调用）。
// 这个函数由 trampoline.S 中的 uservec 调用，是用户态陷入内核的入口。
//
void
usertrap(void)
{
  int which_dev = 0;

  // 检查 sstatus 寄存器的 SPP 位（Supervisor Previous Privilege）。
  // 如果 SPP 位不为 0，说明陷入不是来自用户模式（U-mode），这是一个严重错误。
  // xv6 设计中，usertrap 必须由用户态代码触发。
  if((r_sstatus() & SSTATUS_SPP) != 0)
    panic("usertrap: not from user mode");

  // 由于现在已经进入内核态，我们将 stvec 重新指向 kernelvec。
  // 这样做是为了处理嵌套中断：如果在处理当前用户陷阱的过程中，发生了新的中断（如时钟中断），
  // CPU 会跳转到 kernelvec，由 kerneltrap() 接管，而不是再次进入 usertrap。
  w_stvec((uint64)kernelvec);

  struct proc *p = myproc();
  
  // 保存用户程序的程序计数器（PC）。sepc 寄存器保存了陷入发生时的指令地址。
  // 我们把它保存到进程的陷阱帧中，以便在处理完毕后能返回到正确的位置继续执行。
  p->trapframe->epc = r_sepc();
  
  // 检查 scause 寄存器来确定陷阱的原因。
  // 如果 scause 的值是 8，表示这是一个由 `ecall` 指令引发的系统调用。
  if(r_scause() == 8){
    // 系统调用处理流程

    // 如果当前进程已经被标记为“killed”（例如被其他进程调用kill()），
    // 则不执行系统调用，直接退出。
    if(killed(p))
      exit(-1);

    // ecall 指令执行后，sepc 指向 ecall 本身。为了让程序在返回用户空间后
    // 能从下一条指令继续执行，我们需要将保存的 epc 值加 4 (RISC-V指令长度为4字节)。
    p->trapframe->epc += 4;

    // 在处理系统调用之前，先打开中断。
    // 到目前为止，所有与陷阱相关的关键寄存器（sepc, sstatus, scause）都已保存，
    // 此时可以安全地响应其他中断（特别是时钟中断），以保证系统的响应性。
    intr_on();

    // 调用 syscall() 函数来分发和执行具体的系统调用。
    syscall();
  } else if((which_dev = devintr()) != 0){
    // 设备中断处理流程 (devintr() 返回非零值表示有中断发生)。
    // devintr() 函数内部会处理具体的中断逻辑（如读写串口、磁盘等）。
    // 这里不需要做额外的事情。
  } else {
    // 未知陷阱处理流程。
    // 既不是系统调用，也不是已知的设备中断，说明发生了意外的异常。
    // 例如：访问了非法的内存地址（缺页）、执行了非法指令等。
    printf("usertrap(): unexpected scause 0x%lx pid=%d\n", r_scause(), p->pid);
    printf("            sepc=0x%lx stval=0x%lx\n", r_sepc(), r_stval());
    // 标记该进程为 "killed"，使其在返回用户空间前被终止。
    setkilled(p);
  }

  // 再次检查进程是否在处理陷阱的过程中被杀死。
  // 例如，一个时钟中断可能导致了进程时间片用完而被调度器切换，或者被其他进程杀死。
  if(killed(p))
    exit(-1);

  // 如果这是一个时钟中断 (devintr() 返回2)，说明当前进程的时间片可能已用完。
  // 调用 yield() 主动放弃 CPU，让调度器选择另一个进程运行，从而实现抢占式调度。
  if(which_dev == 2)
    yield();

  // 调用 usertrapret() 准备返回用户空间。
  usertrapret();
}

//
// 准备恢复上下文并返回用户空间。
//
void
usertrapret(void)
{
  struct proc *p = myproc();

  // 在切换回用户态之前，关闭中断。
  // 这是为了防止在恢复用户上下文的过程中（例如修改 stvec、sstatus）发生中断，
  // 导致状态不一致。中断将在执行 `sret` 指令返回用户态后自动重新开启。
  intr_off();

  // 设置 stvec，使其指向用户陷阱处理程序的入口。
  // TRAMPOLINE 页被映射到虚拟地址空间的固定高地址，用户和内核都能访问。
  // uservec 是 trampoline.S 中的一个标签，我们计算出它的绝对虚拟地址。
  // 当下一次在用户空间发生陷阱时，CPU 将跳转到这个地址。
  uint64 trampoline_uservec = TRAMPOLINE + (uservec - trampoline);
  w_stvec(trampoline_uservec);

  // 准备陷阱帧（trapframe）中的内核相关信息，供下一次陷入时使用。
  // 当下一次从用户态陷入时，trampoline.S 中的代码需要这些信息来正确设置内核环境。
  p->trapframe->kernel_satp = r_satp();         // 保存当前内核页表的地址。
  p->trapframe->kernel_sp = p->kstack + PGSIZE; // 保存当前进程的内核栈顶地址。
  p->trapframe->kernel_trap = (uint64)usertrap;  // 保存 usertrap 函数的地址。
  p->trapframe->kernel_hartid = r_tp();         // 保存当前 hart (CPU核心) 的 id。

  // 设置 sret 指令返回用户空间所需的 sstatus 寄存器。
  unsigned long x = r_sstatus();
  x &= ~SSTATUS_SPP; // 1. 清除 SPP 位，将 sret 后的模式设置为用户模式 (U-mode)。
  x |= SSTATUS_SPIE; // 2. 设置 SPIE 位 (Supervisor Previous Interrupt Enable)，
                     //    使得 sret 执行后，中断能被自动重新开启。
  w_sstatus(x);

  // 设置 sepc (Supervisor Exception Program Counter)，
  // 将其指向我们之前保存的、用户代码应该继续执行的地址。
  // sret 指令会跳转到 sepc 指定的地址。
  w_sepc(p->trapframe->epc);

  // 生成传递给 userret 的 satp 值。
  // 这个 satp 值包含了用户页表的物理地址，userret 会用它来切换地址空间。
  uint64 satp = MAKE_SATP(p->pagetable);

  // 计算 userret 在 TRAMPOLINE 页中的绝对虚拟地址。
  // 然后，像函数调用一样跳转到这个地址。
  // userret 的汇编代码会负责：
  // 1. 使用 satp 切换到用户页表。
  // 2. 从陷阱帧中恢复所有通用寄存器 (a0-a7, t0-t6, etc.)。
  // 3. 执行 `sret` 指令，原子地切换回用户模式，并跳转到 sepc 指定的地址。
  uint64 trampoline_userret = TRAMPOLINE + (userret - trampoline);
  ((void (*)(uint64))trampoline_userret)(satp);
}

//
// 内核态中发生的中断和异常会通过 kernelvec 跳转到这里。
// 这个函数在当前进程的内核栈上执行。
//
void 
kerneltrap()
{
  int which_dev = 0;
  uint64 sepc = r_sepc();
  uint64 sstatus = r_sstatus();
  uint64 scause = r_scause();
  
  // 检查 sstatus 的 SPP 位，确保陷阱确实来自内核态（supervisor mode）。
  // 如果不是，说明系统状态异常。
  if((sstatus & SSTATUS_SPP) == 0)
    panic("kerneltrap: not from supervisor mode");
  // 内核态处理陷阱时，中断应该是关闭的。如果不是，同样是异常情况。
  if(intr_get() != 0)
    panic("kerneltrap: interrupts enabled");

  // 调用 devintr() 检查是否是设备中断。
  if((which_dev = devintr()) == 0){
    // 如果不是设备中断，说明是内核代码本身发生了异常（例如访问了无效指针）。
    // 这是严重的、不可恢复的错误，打印调试信息并终止内核。
    printf("scause(管理者原因寄存器) 0x%lx\n",  scause);
    printf("sepc(管理者异常程序计数器) 0x%lx\n", r_sepc());
    printf("stval(管理者陷阱值寄存器) 0x%lx\n", r_stval());
    panic("kerneltrap");
  }

  // 如果是时钟中断，并且当前有一个正在运行的进程，
  // 则调用 yield() 让出 CPU，以实现内核任务的协作式调度或用户进程的抢占。
  if(which_dev == 2 && myproc() != 0)
    yield();

  // 恢复 sepc 和 sstatus 寄存器。
  // 这是因为 yield() 中可能发生上下文切换（swtch），这会修改这些寄存器。
  // 为了确保 kernelvec.S 中的 `sret` 能返回到内核陷入前的正确位置，
  // 必须在返回前恢复它们。
  w_sepc(sepc);
  w_sstatus(sstatus);
}

//
// 时钟中断处理函数。由 devintr() 在检测到时钟中断时调用。
//
void
clockintr()
{
  // 只在 0 号 CPU 上处理时钟滴答，以避免多核竞争。
  if(cpuid() == 0){
  acquire(&tickslock);
  ticks++;
  // 唤醒所有因调用 sleep() 而在 `ticks` 这个“通道”上等待的进程。
  wakeup(&ticks);
  release(&tickslock);
}

  // 向 CLINT (Core-Level Interruptor) 设置下一次时钟中断的时间。
  // 这也会清除当前的中断请求。1000000 大约是 0.1 秒。
  w_stimecmp(r_time() + 1000000);
}

//
// 检查中断是由哪个设备引起的，并调用相应的处理函数。
// 返回值: 
//   2 表示时钟中断 (timer interrupt)
//   1 表示其他设备中断 (如 UART, virtio-disk)
//   0 表示不是设备中断，或者无法识别
//
int
devintr()
{
  uint64 scause = r_scause();

  // scause 的最高位（即 bit 63）为 1 表示这是一个中断（asynchronous interrupt），
  // 否则是异常（synchronous exception）。低位则表示具体的中断/异常类型。
  
  // 检查是否是 supervisor-mode external interrupt (中断号 9)。
  // 外部设备（如磁盘、串口、网卡）的中断都属于这类。
  if((scause & 0x8000000000000000L) && (scause & 0xff) == 9){
    // 是外部中断，需要通过 PLIC (Platform-Level Interrupt Controller) 来确定具体来源。
    
    // 向 PLIC "认领" (claim) 一个挂起的中断号。
    int irq = plic_claim();

    if(irq == UART0_IRQ){
      // 是 UART (串口) 中断，调用其处理函数。
      uartintr();
    } else if(irq == VIRTIO0_IRQ){
      // 是 Virtio 磁盘中断，调用其处理函数。
      virtio_disk_intr();
    } else if(irq){
      // 收到一个未知的设备中断号。
      printf("unexpected interrupt irq=%d\n", irq);
    }

    // 如果成功认领了一个中断 (irq != 0)，
    // 必须通知 PLIC 该中断已处理完毕 (complete)。
    // 这样 PLIC 才会允许该设备再次触发中断。
    if(irq)
      plic_complete(irq);

    return 1;
  } else if(scause == 0x8000000000000001L) {
    // 检查是否是 supervisor-mode software interrupt (中断号 1)。
    // 在 xv6 中，这被用于处理跨核的中断请求（IPI），但在此处未处理。
    // a real os would do something with this.
    // 真正的操作系统会在这里做些事情。
    return 0; // 暂不处理，返回0
  } else if(scause == 0x8000000000000005L){
    // 检查是否是 supervisor-mode timer interrupt (中断号 5)。
    // 这是由 CLINT (Core-Level Interruptor) 产生的时钟中断。
    // 在RISC-V中，时钟中断是每个核心私有的，所以我们检查一下
    // `sip` 寄存器来确认。
    if(cpuid() == 0) {
      clockintr();
    }
    
    // 清除 supervisor timer interrupt pending bit in sip.
    // 在 `sip` 寄存器中清除监管者模式时钟中断的挂起位。
    w_sip(r_sip() & ~2);

    return 2;
  } else {
    // 其他未知类型的中断或异常。
    return 0;
  }
}
