// 专用于内核上下文切换时保存的寄存器。
// RISC-V的调用约定中，callee-saved寄存器（s0-s11）由被调用函数负责保存和恢复。
// `ra` 是返回地址（return address），`sp` 是栈指针（stack pointer）。
// 当一个函数（调用者）调用另一个函数（被调用者）时，被调用者不能随意修改这些寄存器，
// 如果需要使用，必须在函数开始时将它们的值保存在栈上，在函数返回前再从栈上恢复。
// 这样做可以确保调用者函数在子函数返回后，其寄存器状态保持不变。
struct context {
  uint64 ra;  // 返回地址，指向调用`swtch`后应返回执行的指令。
  uint64 sp;  // 栈指针，指向当前内核栈的栈顶。

  // Callee-saved registers (被调用者保存的寄存器)
  uint64 s0;
  uint64 s1;
  uint64 s2;
  uint64 s3;
  uint64 s4;
  uint64 s5;
  uint64 s6;
  uint64 s7;
  uint64 s8;
  uint64 s9;
  uint64 s10;
  uint64 s11;
};

// 每个CPU核心维护一个`cpu`结构体，记录其自身的状态信息。
struct cpu {
  struct proc *proc;      // 指向当前在此CPU上执行的进程，若为null，则表示CPU空闲。
  struct context context; // `swtch`函数会在此保存上下文，以便切换到调度器线程。
  int noff;               // 调用`push_off()`的嵌套层数，用于管理中断禁用。
  int intena;             // 在调用`push_off()`之前，中断是否是开启状态。
};

extern struct cpu cpus[NCPU]; // `NCPU`是系统中CPU核心的数量，这是一个`cpu`结构体数组。

// `trapframe`结构体用于在用户态和内核态之间切换时，保存和恢复进程的上下文。
// 当发生系统调用、异常或中断时，硬件或软件会将用户进程的寄存器等状态保存在这个结构体中，
// 以便在内核处理完毕后，能够精确地恢复到用户态的执行点。
// `uservec` (定义于 trampoline.S) 会将用户寄存器保存到`trapframe`，
// 然后从`trapframe`中加载`kernel_sp`, `kernel_hartid`, `kernel_satp`等内核关键信息，
// 并跳转到`kernel_trap`开始内核处理流程。
// `usertrapret`和`userret` (定义于 trampoline.S) 则执行相反的操作，
// 它们设置`trapframe`中的`kernel_*`字段，恢复用户寄存器，切换回用户页表，然后返回用户空间。
// `trapframe`也保存了callee-saved寄存器 (s0-s11)，因为从内核态返回用户态的过程
// 不会经过完整的函数调用栈返回，需要手动恢复这些寄存器。
struct trapframe {
  /*   0 */ uint64 kernel_satp;   // 内核页表的地址
  /*   8 */ uint64 kernel_sp;     // 当前进程的内核栈顶指针
  /*  16 */ uint64 kernel_trap;   // `usertrap()`函数的地址，是所有用户态陷阱的入口点
  /*  24 */ uint64 epc;           // 异常程序计数器 (Exception Program Counter)，保存发生陷阱时的指令地址
  /*  32 */ uint64 kernel_hartid; // 当前执行的CPU核心ID
  /*  40 */ uint64 ra;            // 返回地址 (Return Address)
  /*  48 */ uint64 sp;            // 用户栈指针 (Stack Pointer)
  /*  56 */ uint64 gp;            // 全局指针 (Global Pointer)
  /*  64 */ uint64 tp;            // 线程指针 (Thread Pointer)
  /*  72 */ uint64 t0;            // 临时寄存器 (Temporary/alternate link register)
  /*  80 */ uint64 t1;            // 临时寄存器
  /*  88 */ uint64 t2;            // 临时寄存器
  /*  96 */ uint64 s0;            // 保存的寄存器/帧指针 (Saved register/frame pointer)
  /* 104 */ uint64 s1;            // 保存的寄存器
  /* 112 */ uint64 a0;            // 函数参数/返回值 (Function argument/return value)
  /* 120 */ uint64 a1;            // 函数参数/返回值
  /* 128 */ uint64 a2;            // 函数参数
  /* 136 */ uint64 a3;            // 函数参数
  /* 144 */ uint64 a4;            // 函数参数
  /* 152 */ uint64 a5;            // 函数参数
  /* 160 */ uint64 a6;            // 函数参数
  /* 168 */ uint64 a7;            // 函数参数
  /* 176 */ uint64 s2;            // 保存的寄存器
  /* 184 */ uint64 s3;            // 保存的寄存器
  /* 192 */ uint64 s4;            // 保存的寄存器
  /* 200 */ uint64 s5;            // 保存的寄存器
  /* 208 */ uint64 s6;            // 保存的寄存器
  /* 216 */ uint64 s7;            // 保存的寄存器
  /* 224 */ uint64 s8;            // 保存的寄存器
  /* 232 */ uint64 s9;            // 保存的寄存器
  /* 240 */ uint64 s10;           // 保存的寄存器
  /* 248 */ uint64 s11;           // 保存的寄存器
  /* 256 */ uint64 t3;            // 临时寄存器
  /* 264 */ uint64 t4;            // 临时寄存器
  /* 272 */ uint64 t5;            // 临时寄存器
  /* 280 */ uint64 t6;            // 临时寄存器
};

// 定义进程可能处于的几种状态
enum procstate {
  UNUSED,   // 进程槽位未使用
  USED,     // 进程正在初始化，尚未准备好运行
  SLEEPING, // 进程正在等待某个事件（如I/O完成、管道数据等）
  RUNNABLE, // 进程已准备就绪，等待调度器分配CPU
  RUNNING,  // 进程当前正在CPU上执行
  ZOMBIE    // 进程已终止，但其父进程尚未通过 `wait()` 回收其资源
};


// `proc`结构体是进程控制块（PCB），包含了单个进程的所有状态信息。
struct proc {
  struct spinlock lock; // 保护该进程结构体的自旋锁

  // p->lock 保护以下字段：
  enum procstate state;        // 进程当前的状态 (SLEEPING, RUNNABLE, etc.)
  void *chan;                  // 如果进程处于SLEEPING状态，chan指向其等待的通道，否则为0
  int killed;                  // 如果非零，表示该进程已被标记为杀死
  int xstate;                  // 进程的退出状态码，由父进程通过`wait()`获取
  int pid;                     // 唯一的进程标识符 (Process ID)

  // wait_lock 保护以下字段（用于父子进程间的等待关系）：
  struct proc *parent;         // 指向父进程的指针

  // 以下字段为进程私有，通常只在进程自己的上下文中修改，因此大多时候不需要锁保护
  uint64 kstack;               // 为该进程分配的内核栈的虚拟地址
  uint64 sz;                   // 进程的用户内存大小（以字节为单位）
  pagetable_t pagetable;       // 指向该进程的用户页表的指针
  struct trapframe *trapframe; // 指向该进程的陷阱帧（trapframe）的指针
  struct context context;      // 用于在`swtch`时保存和恢复进程的内核上下文
  struct file *ofile[NOFILE];  // 进程打开的文件描述符表
  struct inode *cwd;           // 进程的当前工作目录
  char name[16];               // 进程的名称 (主要用于调试)
};
