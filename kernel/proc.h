//-*- coding: utf-8 -*-
// 为内核上下文切换保存的寄存器。
struct context
{
  uint64 ra;
  uint64 sp;

  // callee-saved
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

// 每CPU状态。
struct cpu
{
  struct proc *proc;      // 在此cpu上运行的进程，或为null。
  struct context context; // swtch() 到这里进入调度程序。
  int noff;               // push_off() 嵌套的深度。
  int intena;             // 在 push_off() 之前中断是否启用？
};

extern struct cpu cpus[NCPU];

// trampoline.S中陷阱处理代码的每进程数据。
// 在用户页表中，它自己占用一个页面，就在trampoline页面下面。
// 在内核页表中没有特殊映射。
// trampoline.S中的uservec将用户寄存器保存在trapframe中，
// 然后从trapframe的kernel_sp, kernel_hartid, kernel_satp初始化寄存器，
// 并跳转到kernel_trap。
// trampoline.S中的usertrapret()和userret设置trapframe的kernel_*，
// 从trapframe恢复用户寄存器，切换到用户页表，并进入用户空间。
// trapframe包括被调用者保存的用户寄存器，如s0-s11，因为
// 通过usertrapret()返回用户的路径不会通过整个内核调用栈返回。
struct trapframe
{
  /*   0 */ uint64 kernel_satp;   // 内核页表
  /*   8 */ uint64 kernel_sp;     // 进程内核栈的顶部
  /*  16 */ uint64 kernel_trap;   // usertrap()
  /*  24 */ uint64 epc;           // 保存的用户程序计数器
  /*  32 */ uint64 kernel_hartid; // 保存的内核tp
  /*  40 */ uint64 ra;
  /*  48 */ uint64 sp;
  /*  56 */ uint64 gp;
  /*  64 */ uint64 tp;
  /*  72 */ uint64 t0;
  /*  80 */ uint64 t1;
  /*  88 */ uint64 t2;
  /*  96 */ uint64 s0;
  /* 104 */ uint64 s1;
  /* 112 */ uint64 a0;
  /* 120 */ uint64 a1;
  /* 128 */ uint64 a2;
  /* 136 */ uint64 a3;
  /* 144 */ uint64 a4;
  /* 152 */ uint64 a5;
  /* 160 */ uint64 a6;
  /* 168 */ uint64 a7;
  /* 176 */ uint64 s2;
  /* 184 */ uint64 s3;
  /* 192 */ uint64 s4;
  /* 200 */ uint64 s5;
  /* 208 */ uint64 s6;
  /* 216 */ uint64 s7;
  /* 224 */ uint64 s8;
  /* 232 */ uint64 s9;
  /* 240 */ uint64 s10;
  /* 248 */ uint64 s11;
  /* 256 */ uint64 t3;
  /* 264 */ uint64 t4;
  /* 272 */ uint64 t5;
  /* 280 */ uint64 t6;
};

enum procstate
{
  UNUSED,
  USED,
  SLEEPING,
  RUNNABLE,
  RUNNING,
  ZOMBIE
};

// 每进程状态
struct proc
{
  struct spinlock lock;

  // 使用这些时必须持有p->lock：
  enum procstate state; // 进程状态
  void *chan;           // 如果非零，则在chan上休眠
  int killed;           // 如果非零，则已被杀死
  int xstate;           // 要返回给父进程wait的退出状态
  int pid;              // 进程ID

  // 使用此项时必须持有wait_lock：
  struct proc *parent; // 父进程

  // 这些是进程私有的，因此不需要持有p->lock。
  uint64 kstack;               // 内核栈的虚拟地址
  uint64 sz;                   // 进程内存大小（字节）
  pagetable_t pagetable;       // 用户页表
  struct trapframe *trapframe; // trampoline.S的数据页
  struct context context;      // swtch() 到这里运行进程
  struct file *ofile[NOFILE];  // 打开的文件
  struct inode *cwd;           // 当前目录
  char name[16];               // 进程名（用于调试）
};