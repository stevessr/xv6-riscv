#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "proc.h"
#include "defs.h"

// 全局时钟滴答计数器和它的锁
struct spinlock tickslock;
uint ticks;

// 从 trampoline.S 导入的符号，它们是汇编代码的地址
extern char trampoline[], uservec[], userret[];

// 在 kernelvec.S 中定义的内核陷入向量，它会调用 kerneltrap()
void kernelvec();

// 在下方定义的设备中断处理函数
extern int devintr();

void
trapinit(void)
{
  // 初始化时钟锁
  initlock(&tickslock, "time");
}

// 为每个 hart (CPU核心) 设置内核陷入处理程序
void
trapinithart(void)
{
  // 将 stvec 寄存器设置为 kernelvec 的地址。
  // 当 CPU 处于内核态（supervisor mode）时，发生的所有中断、异常都会跳转到 kernelvec 执行。
  w_stvec((uint64)kernelvec);
}

//
// 处理来自用户空间的 中断、异常 或 系统调用。
// 这个函数由 trampoline.S 中的 uservec 调用。
//
void
usertrap(void)
{
  int which_dev = 0;

  // sstatus 的 SPP 位记录了陷入前的特权级别。
  // 如果 SPP 不为 0，说明陷入不是来自用户模式，这是异常情况。
  if((r_sstatus() & SSTATUS_SPP) != 0)
    panic("usertrap: not from user mode");

  // 由于我们现在已经在内核中，所以将陷入向量设置为 kernelvec。
  // 这样，如果在处理当前陷入的过程中发生新的中断（例如时钟中断），
  // CPU会跳转到 kernelvec，由 kerneltrap() 处理。
  w_stvec((uint64)kernelvec);

  struct proc *p = myproc();
  
  // 保存用户程序的程序计数器（PC），即陷入发生时的指令地址。
  p->trapframe->epc = r_sepc();
  
  // 检查 scause 寄存器以确定陷入的原因。
  // scause == 8 表示是由 `ecall` 指令引发的系统调用。
  if(r_scause() == 8){
    // 是系统调用

    // 如果进程已经被标记为 killed，则直接退出。
    if(killed(p))
      exit(-1);

    // sepc 指向 ecall 指令本身，我们需要让它指向下一条指令，
    // 以便在返回用户空间后继续执行。
    p->trapframe->epc += 4;

    // 到目前为止，我们已经处理完 sepc, scause, sstatus 这些可能被中断修改的寄存器，
    // 所以现在可以安全地打开中断了。
    intr_on();

    // 调用系统调用处理函数
    syscall();
  } else if((which_dev = devintr()) != 0){
    // 是一个设备中断 (devintr() 返回非零值)
    // ok，devintr() 内部已经处理了中断。
  } else {
    // 其他未知类型的陷入（例如缺页异常、非法指令等）。
    printf("usertrap(): unexpected scause 0x%lx pid=%d\n", r_scause(), p->pid);
    printf("            sepc=0x%lx stval=0x%lx\n", r_sepc(), r_stval());
    // 标记该进程为 killed，使其在下次检查时退出。
    setkilled(p);
  }

  // 再次检查进程是否在处理陷入期间被杀死（例如被其他CPU上的进程kill）。
  if(killed(p))
    exit(-1);

  // 如果这是一个时钟中断 (devintr() 返回 2)，则调用 yield() 主动放弃CPU，
  // 以实现进程的抢占式调度。
  if(which_dev == 2)
    yield();

  // 调用 usertrapret() 准备返回用户空间。
  usertrapret();
}

//
// 准备返回用户空间
//
void
usertrapret(void)
{
  struct proc *p = myproc();

  // 我们即将把陷入目标从 kerneltrap() 改回 usertrap()，
  // 在此之前先关闭中断，直到完全返回用户空间，
  // 以确保中断在正确的模式下被正确处理。
  intr_off();

  // 计算 uservec 在 TRAMPOLINE 页中的绝对虚拟地址，并设置 stvec。
  // 当下一次在用户空间发生陷入时，CPU 将跳转到这个地址。
  uint64 trampoline_uservec = TRAMPOLINE + (uservec - trampoline);
  w_stvec(trampoline_uservec);

  // 设置陷阱帧（trapframe）中的内核相关信息，
  // 以便下一次陷入时 trampoline.S 中的代码能够使用。
  p->trapframe->kernel_satp = r_satp();         // 保存内核页表的地址
  p->trapframe->kernel_sp = p->kstack + PGSIZE; // 保存进程的内核栈顶地址
  p->trapframe->kernel_trap = (uint64)usertrap;  // 保存 usertrap 函数的地址
  p->trapframe->kernel_hartid = r_tp();         // 保存当前 hart id

  // 设置 sret 指令返回用户空间所需的寄存器。
  
  // 设置 sstatus 寄存器：
  // 1. 清除 SPP 位 (Supervisor Previous Privilege)，表示 sret 后将进入用户模式。
  // 2. 设置 SPIE 位 (Supervisor Previous Interrupt Enable)，允许在返回用户模式后开启中断。
  unsigned long x = r_sstatus();
  x &= ~SSTATUS_SPP; // 清除 SPP 位
  x |= SSTATUS_SPIE; // 设置 SPIE 位
  w_sstatus(x);

  // 设置 sepc (Supervisor Exception Program Counter)，
  // 指示 sret 指令返回到用户代码的哪个位置（之前已保存）。
  w_sepc(p->trapframe->epc);

  // 告诉 trampoline.S 需要切换到哪个用户页表。
  uint64 satp = MAKE_SATP(p->pagetable);

  // 计算 userret 在 TRAMPOLINE 页中的地址，并跳转过去。
  // userret 会负责切换页表、恢复所有用户寄存器，并执行 sret 指令返回用户态。
  uint64 trampoline_userret = TRAMPOLINE + (userret - trampoline);
  ((void (*)(uint64))trampoline_userret)(satp);
}

//
// 来自内核代码的中断和异常通过 kernelvec 跳转到这里。
// 在当前进程的内核栈上执行。
//
void 
kerneltrap()
{
  int which_dev = 0;
  uint64 sepc = r_sepc();
  uint64 sstatus = r_sstatus();
  uint64 scause = r_scause();
  
  // 检查 sstatus 的 SPP 位，确认陷入确实来自监管者模式。
  if((sstatus & SSTATUS_SPP) == 0)
    panic("kerneltrap: not from supervisor mode");
  // 检查中断是否关闭，内核陷入时中断应该是关闭的。
  if(intr_get() != 0)
    panic("kerneltrap: interrupts enabled");

  if((which_dev = devintr()) == 0){
    // 非设备中断（例如异常）。
    printf("scause=0x%lx sepc=0x%lx stval=0x%lx\n", scause, r_sepc(), r_stval());
    panic("kerneltrap");
  }

  // 如果是时钟中断，并且当前有正在运行的进程，则让出CPU。
  if(which_dev == 2 && myproc() != 0)
    yield();

  // yield() 可能会导致其他陷入（例如在切换任务时），
  // 所以需要恢复 sepc 和 sstatus，以确保 kernelvec.S 中的 sret 能正确返回。
  w_sepc(sepc);
  w_sstatus(sstatus);
}

// 时钟中断处理函数
void
clockintr()
{
  // 只在 0 号 CPU 上处理时钟滴答，以避免多核竞争。
  if(cpuid() == 0){
    acquire(&tickslock);
    ticks++;
    // 唤醒所有在 ticks 通道上等待的进程（例如 sleep 系统调用）。
    wakeup(&ticks);
    release(&tickslock);
  }

  // 向 CLINT (Core-Level Interruptor) 设置下一次时钟中断的时间。
  // 这也会清除当前的中断请求。1000000 大约是 0.1 秒。
  w_stimecmp(r_time() + 1000000);
}

//
// 检查是外部中断还是软件中断，并处理它。
// 返回值: 2 表示时钟中断，1 表示其他设备中断，0 表示无法识别。
//
int
devintr()
{
  uint64 scause = r_scause();

  // scause 的最高位为1表示是中断，后面的值表示中断类型。
  // 0x8000000000000009L 是监管者模式的外部中断。
  if(scause == 0x8000000000000009L){
    // 来自 PLIC (Platform-Level Interrupt Controller) 的外部中断。

    // 向 PLIC 查询是哪个设备触发了中断。
    int irq = plic_claim();

    if(irq == UART0_IRQ){
      // UART (串口) 中断
      uartintr();
    } else if(irq == VIRTIO0_IRQ){
      // Virtio 磁盘中断
      virtio_disk_intr();
    } else if(irq){
      // 未知设备中断
      printf("unexpected interrupt irq=%d\n", irq);
    }

    // 如果 irq 非零，表示我们处理了一个中断。
    // 通知 PLIC 该中断已处理完毕，允许该设备再次触发中断。
    if(irq)
      plic_complete(irq);

    return 1;
  } else if(scause == 0x8000000000000005L){
    // 监管者模式的时钟中断。
    clockintr();
    return 2;
  } else {
    // 其他类型的中断或异常
    return 0;
  }
}
