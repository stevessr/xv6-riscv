#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "proc.h"
#include "defs.h"

struct cpu cpus[NCPU];

struct proc proc[NPROC];

struct proc *initproc;

int nextpid = 1;
struct spinlock pid_lock;

extern void forkret(void);
static void freeproc(struct proc *p);

extern char trampoline[]; // 定义在 trampoline.S

// 帮助确保对 wait() 中父进程的唤醒不会丢失。
// 在使用 p->parent 时，有助于遵守内存模型。
// 必须在获取任何 p->lock 之前获取此锁。
struct spinlock wait_lock;

// 为每个进程的内核栈分配一个页面。
// 将其映射到高地址内存，后面跟着一个无效的保护页。
void
proc_mapstacks(pagetable_t kpgtbl)
{
  struct proc *p;
  
  for(p = proc; p < &proc[NPROC]; p++) {
    char *pa = kalloc();
    if(pa == 0)
      panic("kalloc");
    uint64 va = KSTACK((int) (p - proc));
    kvmmap(kpgtbl, va, (uint64)pa, PGSIZE, PTE_R | PTE_W);
  }
}

// 初始化进程表和相关锁。
// 在系统启动时由 main 调用。
void
procinit(void)
{
  struct proc *p;
  
  initlock(&pid_lock, "nextpid"); // 初始化 PID 分配锁
  initlock(&wait_lock, "wait_lock"); // 初始化 wait/exit 使用的锁
  for(p = proc; p < &proc[NPROC]; p++) {
      initlock(&p->lock, "proc"); // 初始化每个进程的锁
      p->state = UNUSED; // 标记进程为空闲
      p->kstack = KSTACK((int) (p - proc)); // 计算并设置每个进程的内核栈地址
  }
}

// 返回当前 CPU 的 ID (hartid)。
// 调用时必须禁用中断，以避免调度器将进程移到其他 CPU。
// r_tp() 读取 RISC-V 的 tp 寄存器，该寄存器在启动时被设置为 CPU ID。
int
cpuid()
{
  int id = r_tp();
  return id;
}

// 返回当前 CPU 的 `struct cpu` 指针。
// 调用时必须禁用中断。
struct cpu*
mycpu(void)
{
  int id = cpuid();
  struct cpu *c = &cpus[id];
  return c;
}

// 返回当前 CPU 上正在运行的进程的 `struct proc` 指针。
// 如果没有进程在运行，则返回零。
// `push_off`/`pop_off` 用于禁用/启用中断，以确保读取 `c->proc` 的原子性。
struct proc*
myproc(void)
{
  push_off();
  struct cpu *c = mycpu();
  struct proc *p = c->proc;
  pop_off();
  return p;
}

// 分配一个唯一的进程 ID (PID)。
// 使用自旋锁 `pid_lock` 来保护 `nextpid` 的原子性，防止多个进程同时分配到相同的 PID。
int
allocpid()
{
  int pid;
  
  acquire(&pid_lock);
  pid = nextpid;
  nextpid = nextpid + 1;
  release(&pid_lock);

  return pid;
}

// 在进程表中查找一个状态为 UNUSED 的进程槽位。
// 如果找到，则初始化其内核状态，并持有 p->lock 返回。
// 如果没有可用的进程槽位或内存分配失败，则返回 0。
static struct proc*
allocproc(void)
{
  struct proc *p;

  for(p = proc; p < &proc[NPROC]; p++) {
    acquire(&p->lock);
    if(p->state == UNUSED) {
      goto found; // 找到空闲槽位
    } else {
      release(&p->lock);
    }
  }
  return 0; // 没有找到空闲进程

found:
  p->pid = allocpid();
  p->state = USED;

  // 分配一个陷阱帧页面。
  if((p->trapframe = (struct trapframe *)kalloc()) == 0){
    freeproc(p);
    release(&p->lock);
    return 0;
  }

  // 创建一个空的用户页表。
  p->pagetable = proc_pagetable(p);
  if(p->pagetable == 0){
    freeproc(p);
    release(&p->lock);
    return 0;
  }

  // 设置新的上下文，使其在内核栈上开始执行 forkret。
  // forkret 最终会返回到用户空间。
  memset(&p->context, 0, sizeof(p->context));
  p->context.ra = (uint64)forkret; // 设置返回地址为 forkret
  p->context.sp = p->kstack + PGSIZE; // 设置栈指针为内核栈顶

  return p;
}

// 释放一个进程结构体及其相关的资源（如用户内存、陷阱帧）。
// 调用时必须持有 p->lock。
static void
freeproc(struct proc *p)
{
  if(p->trapframe)
    kfree((void*)p->trapframe); // 释放陷阱帧
  p->trapframe = 0;
  if(p->pagetable)
    proc_freepagetable(p->pagetable, p->sz); // 释放页表和用户内存
  p->pagetable = 0;
  p->sz = 0;
  p->pid = 0;
  p->parent = 0;
  p->name[0] = 0;
  p->chan = 0;
  p->killed = 0;
  p->xstate = 0;
  p->state = UNUSED; // 标记进程为空闲
}

// 为给定进程创建一个用户页表。
// 该页表初始时只包含 trampoline 和 trapframe 的映射，没有用户内存。
pagetable_t
proc_pagetable(struct proc *p)
{
  pagetable_t pagetable;

  // 创建一个空的页表。
  pagetable = uvmcreate();
  if(pagetable == 0)
    return 0;

  // 将 trampoline 代码映射到用户虚拟地址空间的顶部。
  // TRAMPOLINE 是一个固定的虚拟地址。
  // `trampoline` 是链接器脚本中定义的物理地址。
  // PTE_R | PTE_X 表示页面可读、可执行，但不可写。
  // 没有设置 PTE_U，因为这部分内存只在 supervisor 模式下访问。
  if(mappages(pagetable, TRAMPOLINE, PGSIZE,
              (uint64)trampoline, PTE_R | PTE_X) < 0){
    uvmfree(pagetable, 0);
    return 0;
  }

  // 将陷阱帧页面映射到 trampoline 下方。
  // TRAPFRAME 是一个固定的虚拟地址。
  // p->trapframe 是刚分配的物理页。
  // PTE_R | PTE_W 表示页面可读、可写。
  if(mappages(pagetable, TRAPFRAME, PGSIZE,
              (uint64)(p->trapframe), PTE_R | PTE_W) < 0){
    uvmunmap(pagetable, TRAMPOLINE, 1, 0);
    uvmfree(pagetable, 0);
    return 0;
  }

  return pagetable;
}

// 释放一个进程的页表以及它引用的所有物理内存。
void
proc_freepagetable(pagetable_t pagetable, uint64 sz)
{
  uvmunmap(pagetable, TRAMPOLINE, 1, 0); // 解除 trampoline 的映射
  uvmunmap(pagetable, TRAPFRAME, 1, 0); // 解除陷阱帧的映射
  uvmfree(pagetable, sz); // 释放所有用户内存页和页表本身
}

// `initcode` 是一个小的用户程序，它执行 `exec("/init")`。
// 这是第一个在用户空间运行的程序。
// 它的二进制代码是硬编码的，从 ../user/initcode.S 汇编而来。
uchar initcode[] = {
  0x17, 0x05, 0x00, 0x00, 0x13, 0x05, 0x45, 0x02,
  0x97, 0x05, 0x00, 0x00, 0x93, 0x85, 0x35, 0x02,
  0x93, 0x08, 0x70, 0x00, 0x73, 0x00, 0x00, 0x00,
  0x93, 0x08, 0x20, 0x00, 0x73, 0x00, 0x00, 0x00,
  0xef, 0xf0, 0x9f, 0xff, 0x2f, 0x69, 0x6e, 0x69,
  0x74, 0x00, 0x00, 0x24, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00
};

// 设置第一个用户进程 (init)。
void
userinit(void)
{
  struct proc *p;

  p = allocproc(); // 分配一个进程
  initproc = p;
  
  // 分配一页用户内存，并将 `initcode` 复制进去。
  uvmfirst(p->pagetable, initcode, sizeof(initcode));
  p->sz = PGSIZE;

  // 准备从内核态到用户态的第一次“返回”。
  p->trapframe->epc = 0;      // 用户程序计数器，从地址 0 开始执行
  p->trapframe->sp = PGSIZE;  // 用户栈指针，指向页面的最高地址

  safestrcpy(p->name, "initcode", sizeof(p->name));
  p->cwd = namei("/"); // 设置当前工作目录为根目录

  p->state = RUNNABLE; // 将进程状态设置为可运行

  release(&p->lock);
}

// 增加或减少当前进程的用户内存 `n` 字节。
// `n` > 0 表示增加，`n` < 0 表示减少。
// 成功返回 0，失败返回 -1。
int
growproc(int n)
{
  uint64 sz;
  struct proc *p = myproc();

  sz = p->sz;
  if(n > 0){
    if((sz = uvmalloc(p->pagetable, sz, sz + n, PTE_W)) == 0) {
      return -1;
    }
  } else if(n < 0){
    sz = uvmdealloc(p->pagetable, sz, sz + n);
  }
  p->sz = sz;
  return 0;
}

// 创建一个新进程，作为当前进程的副本（子进程）。
// 子进程的内核栈被设置为，当它第一次被调度时，会返回到 `forkret`，
// 并且看起来就像是从 `fork()` 系统调用返回一样。
int
fork(void)
{
  int i, pid;
  struct proc *np;
  struct proc *p = myproc();

  // 分配一个新的进程结构体。
  if((np = allocproc()) == 0){
    return -1;
  }

  // 从父进程向子进程复制用户内存。
  if(uvmcopy(p->pagetable, np->pagetable, p->sz) < 0){
    freeproc(np);
    release(&np->lock);
    return -1;
  }
  np->sz = p->sz;

  // 复制父进程保存的陷阱帧（包含了用户寄存器）。
  *(np->trapframe) = *(p->trapframe);

  // 对于子进程，`fork` 系统调用应该返回 0。
  // a0 寄存器用于存放函数返回值。
  np->trapframe->a0 = 0;

  // 复制打开的文件描述符。
  for(i = 0; i < NOFILE; i++)
    if(p->ofile[i])
      np->ofile[i] = filedup(p->ofile[i]);
  np->cwd = idup(p->cwd);

  safestrcpy(np->name, p->name, sizeof(p->name));

  pid = np->pid;

  release(&np->lock);

  // 将子进程的父进程指针指向当前进程。
  acquire(&wait_lock);
  np->parent = p;
  release(&wait_lock);

  // 将子进程的状态设置为可运行。
  acquire(&np->lock);
  np->state = RUNNABLE;
  release(&np->lock);

  return pid; // 在父进程中返回子进程的 PID
}

// 将一个已退出进程 (p) 的所有子进程过继给 init 进程。
// 调用者必须持有 `wait_lock`。
void
reparent(struct proc *p)
{
  struct proc *pp;

  for(pp = proc; pp < &proc[NPROC]; pp++){
    if(pp->parent == p){
      // 这是一个子进程，将其父进程设置为 initproc。
      pp->parent = initproc;
      // 唤醒 init 进程，以防它正在 wait() 中等待子进程退出。
      wakeup(initproc);
    }
  }
}

// 退出当前进程。此函数不会返回。
// 退出的进程会进入 ZOMBIE 状态，直到其父进程调用 wait() 来收集它。
void
exit(int status)
{
  struct proc *p = myproc();

  if(p == initproc)
    panic("init exiting"); // init 进程不应该退出

  // 关闭所有打开的文件。
  for(int fd = 0; fd < NOFILE; fd++){
    if(p->ofile[fd]){
      struct file *f = p->ofile[fd];
      fileclose(f);
      p->ofile[fd] = 0;
    }
  }

  begin_op();
  iput(p->cwd);
  end_op();
  p->cwd = 0;

  acquire(&wait_lock);

  // 将所有子进程过继给 init 进程。
  reparent(p);

  // 唤醒父进程，如果它正在 wait() 中等待。
  wakeup(p->parent);
  
  acquire(&p->lock);

  p->xstate = status; // 保存退出状态
  p->state = ZOMBIE; // 将状态设置为僵尸

  release(&wait_lock);

  // 跳转到调度器，永不返回。
  sched();
  panic("zombie exit"); // sched() 不应该返回
}

// 等待一个子进程退出，并返回其 PID。
// 如果该进程没有子进程，或者被杀死，则返回 -1。
// 如果 `addr` 非零，则将子进程的退出状态复制到 `addr` 指向的用户地址。
int
wait(uint64 addr)
{
  struct proc *pp;
  int havekids, pid;
  struct proc *p = myproc();

  acquire(&wait_lock);

  for(;;){
    // 扫描进程表，查找已退出的子进程。
    havekids = 0;
    for(pp = proc; pp < &proc[NPROC]; pp++){
      if(pp->parent == p){
        // 这是一个子进程。
        // 获取子进程的锁，以确保它不会在此时被 freeproc 释放。
        acquire(&pp->lock);

        havekids = 1;
        if(pp->state == ZOMBIE){
          // 找到了一个僵尸子进程，可以收集它。
          pid = pp->pid;
          if(addr != 0 && copyout(p->pagetable, addr, (char *)&pp->xstate,
                                  sizeof(pp->xstate)) < 0) {
            // 复制退出状态失败。
            release(&pp->lock);
            release(&wait_lock);
            return -1;
          }
          freeproc(pp); // 释放子进程资源
          release(&pp->lock);
          release(&wait_lock);
          return pid;
        }
        release(&pp->lock);
      }
    }

    // 如果没有子进程，或者当前进程被杀死，那么等待就没有意义了。
    if(!havekids || killed(p)){
      release(&wait_lock);
      return -1;
    }
    
    // 等待一个子进程退出。
    // sleep 会原子地释放 wait_lock 并使当前进程休眠。
    sleep(p, &wait_lock);
  }
}

// 每个 CPU 的进程调度器。
// 在系统启动时，每个 CPU 核心都会调用此函数。
// 调度器永不返回，它在一个无限循环中：
//  - 查找一个可运行的进程 (RUNNABLE)。
//  - 使用 swtch 切换到该进程的上下文。
//  - 当进程让出 CPU 时，它会通过 swtch 切换回调度器。
void
scheduler(void)
{
  struct proc *p;
  struct cpu *c = mycpu();
  
  c->proc = 0;
  for(;;){
    // 启用中断，防止在所有进程都休眠时死锁。
    intr_on();

    int found = 0;
    for(p = proc; p < &proc[NPROC]; p++) {
      acquire(&p->lock);
      if(p->state == RUNNABLE) {
        // 找到了一个可运行的进程，切换到它。
        p->state = RUNNING;
        c->proc = p;
        swtch(&c->context, &p->context); // 切换上下文

        // 进程运行完毕，返回到这里。
        // 它应该已经改变了自己的状态（例如，变为 SLEEPING 或 RUNNABLE）。
        c->proc = 0;
        found = 1;
      }
      release(&p->lock);
    }
    if(found == 0) {
      // 没有可运行的进程，CPU 进入低功耗状态，等待中断。
      intr_on();
      asm volatile("wfi"); // Wait For Interrupt
    }
  }
}

// 切换回调度器。
// 调用此函数时，必须持有当前进程的锁 `p->lock`，
// 并且进程状态不能是 RUNNING。
// 它保存当前的中断使能状态，并通过 swtch 切换到调度器的上下文。
void
sched(void)
{
  int intena;
  struct proc *p = myproc();

  if(!holding(&p->lock))
    panic("sched p->lock");
  if(mycpu()->noff != 1)
    panic("sched locks");
  if(p->state == RUNNING)
    panic("sched running");
  if(intr_get())
    panic("sched interruptible");

  intena = mycpu()->intena;
  swtch(&p->context, &mycpu()->context);
  mycpu()->intena = intena;
}

// 主动让出 CPU，给其他进程一个运行的机会。
void
yield(void)
{
  struct proc *p = myproc();
  acquire(&p->lock);
  p->state = RUNNABLE; // 将自己标记为可运行
  sched(); // 切换到调度器
  release(&p->lock);
}

// 当一个 fork 产生的子进程第一次被调度器选中时，
// 它会通过 swtch 进入 `forkret` 函数。
void
forkret(void)
{
  static int first = 1;

  // 此时仍然持有从 scheduler() 获得的 p->lock。
  release(&myproc()->lock);

  if (first) {
    // 第一次执行时，初始化文件系统。
    // 文件系统初始化需要在一个常规进程的上下文中运行（因为它可能会调用 sleep），
    // 所以不能在 main() 中直接调用。
    fsinit(ROOTDEV);

    first = 0;
    // 内存屏障，确保 `first=0` 的写入对其他核心可见。
    __sync_synchronize();
  }

  // 返回到用户空间。
  usertrapret();
}

// 在 `chan` 上原子地休眠，并释放锁 `lk`。
// 当被 `wakeup` 唤醒时，会重新获取锁 `lk`。
void
sleep(void *chan, struct spinlock *lk)
{
  struct proc *p = myproc();
  
  // 必须获取 p->lock 来修改 p->state 并调用 sched()。
  // 一旦持有 p->lock，就不会错过任何 wakeup（因为 wakeup 也会锁定 p->lock），
  // 所以此时释放 `lk` 是安全的。
  acquire(&p->lock);
  release(lk);

  // 进入休眠状态。
  p->chan = chan; // 记录休眠的通道
  p->state = SLEEPING;

  sched(); // 切换到调度器

  // 被唤醒后，从这里继续执行。
  // 清理休眠通道。
  p->chan = 0;

  // 重新获取原来的锁。
  release(&p->lock);
  acquire(lk);
}

// 唤醒所有在 `chan` 上休眠的进程。
// 调用时不能持有任何进程的锁。
void
wakeup(void *chan)
{
  struct proc *p;

  for(p = proc; p < &proc[NPROC]; p++) {
    if(p != myproc()){
      acquire(&p->lock);
      if(p->state == SLEEPING && p->chan == chan) {
        // 找到一个在指定通道上休眠的进程，将其唤醒。
        p->state = RUNNABLE;
      }
      release(&p->lock);
    }
  }
}

// 杀死指定 PID 的进程。
// 被杀死的进程不会立即退出，而是在下一次从内核态返回用户态时
// (在 trap.c 的 usertrap() 中) 检查 `killed` 标志并退出。
int
kill(int pid)
{
  struct proc *p;

  for(p = proc; p < &proc[NPROC]; p++){
    acquire(&p->lock);
    if(p->pid == pid){
      p->killed = 1; // 设置 killed 标志
      if(p->state == SLEEPING){
        // 如果进程正在休眠，则唤醒它，以便它能检查 killed 标志。
        p->state = RUNNABLE;
      }
      release(&p->lock);
      return 0; // 成功
    }
    release(&p->lock);
  }
  return -1; // 没有找到指定 PID 的进程
}

// 设置进程的 killed 标志。
void
setkilled(struct proc *p)
{
  acquire(&p->lock);
  p->killed = 1;
  release(&p->lock);
}

// 检查进程是否被杀死。
int
killed(struct proc *p)
{
  int k;
  
  acquire(&p->lock);
  k = p->killed;
  release(&p->lock);
  return k;
}

// 根据 `user_dst` 的值，将数据从内核空间 (`src`) 复制到
// 用户地址空间 (`dst`) 或内核地址空间 (`dst`)。
// `user_dst` 为 1 表示目标在用户空间。
// 成功返回 0，失败返回 -1。
int
either_copyout(int user_dst, uint64 dst, void *src, uint64 len)
{
  struct proc *p = myproc();
  if(user_dst){
    return copyout(p->pagetable, dst, src, len);
  } else {
    memmove((char *)dst, src, len);
    return 0;
  }
}

// 根据 `user_src` 的值，将数据从用户地址空间 (`src`) 或
// 内核地址空间 (`src`) 复制到内核空间 (`dst`)。
// `user_src` 为 1 表示源在用户空间。
// 成功返回 0，失败返回 -1。
int
either_copyin(void *dst, int user_src, uint64 src, uint64 len)
{
  struct proc *p = myproc();
  if(user_src){
    return copyin(p->pagetable, dst, src, len);
  } else {
    memmove(dst, (char*)src, len);
    return 0;
  }
}

// 向控制台打印进程列表，用于调试。
// 当用户在控制台按下 Ctrl+P 时调用。
// 此函数不获取任何锁，以避免在系统卡住时加剧问题。
void
procdump(void)
{
  static char *states[] = {
  [UNUSED]    "unused",
  [USED]      "used",
  [SLEEPING]  "sleep ",
  [RUNNABLE]  "runble",
  [RUNNING]   "run   ",
  [ZOMBIE]    "zombie"
  };
  struct proc *p;
  char *state;

  printf("\n");
  for(p = proc; p < &proc[NPROC]; p++){
    if(p->state == UNUSED)
      continue;
    if(p->state >= 0 && p->state < NELEM(states) && states[p->state])
      state = states[p->state];
    else
      state = "???";
    printf("%d %s %s", p->pid, state, p->name);
    printf("\n");
  }
}
