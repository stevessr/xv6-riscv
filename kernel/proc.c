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

// 初始化进程表。
void
procinit(void)
{
  struct proc *p;
  
  initlock(&pid_lock, "nextpid");
  initlock(&wait_lock, "wait_lock");
  for(p = proc; p < &proc[NPROC]; p++) {
      initlock(&p->lock, "proc");
      p->state = UNUSED;
      p->kstack = KSTACK((int) (p - proc));
  }
}

// 必须在禁用中断的情况下调用，
// 以防止与进程被移动到不同CPU的竞争。
// 返回当前CPU的ID (hartid)。
int
cpuid()
{
  int id = r_tp();
  return id;
}

// 返回当前CPU的 cpu 结构体指针。
// 调用时必须禁用中断。
struct cpu*
mycpu(void)
{
  int id = cpuid();
  struct cpu *c = &cpus[id];
  return c;
}

// 返回当前CPU上正在运行的进程的 proc 结构体指针，如果没有则返回零。
struct proc*
myproc(void)
{
  push_off();
  struct cpu *c = mycpu();
  struct proc *p = c->proc;
  pop_off();
  return p;
}

// 分配一个唯一的进程ID (PID)。
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

// 在进程表中查找一个 UNUSED 状态的进程。
// 如果找到，则初始化在内核中运行所需的状态，
// 并持有 p->lock 返回。
// 如果没有空闲进程，或内存分配失败，则返回 0。
static struct proc*
allocproc(void)
{
  struct proc *p;

  for(p = proc; p < &proc[NPROC]; p++) {
    acquire(&p->lock);
    if(p->state == UNUSED) {
      goto found;
    } else {
      release(&p->lock);
    }
  }
  return 0;

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

  // 设置新的上下文，使其开始在 forkret 执行，
  // forkret 会返回到用户空间。
  memset(&p->context, 0, sizeof(p->context));
  p->context.ra = (uint64)forkret;
  p->context.sp = p->kstack + PGSIZE;

  return p;
}

// 释放一个 proc 结构体以及其占用的资源，
// 包括用户页。
// 调用时必须持有 p->lock。
static void
freeproc(struct proc *p)
{
  if(p->trapframe)
    kfree((void*)p->trapframe);
  p->trapframe = 0;
  if(p->pagetable)
    proc_freepagetable(p->pagetable, p->sz);
  p->pagetable = 0;
  p->sz = 0;
  p->pid = 0;
  p->parent = 0;
  p->name[0] = 0;
  p->chan = 0;
  p->killed = 0;
  p->xstate = 0;
  p->state = UNUSED;
}

// 为给定进程创建一个用户页表，该页表没有用户内存，
// 但包含 trampoline 和 trapframe 页面。
pagetable_t
proc_pagetable(struct proc *p)
{
  pagetable_t pagetable;

  // 创建一个空的页表。
  pagetable = uvmcreate();
  if(pagetable == 0)
    return 0;

  // 将 trampoline 代码（用于系统调用返回）映射到
  // 用户虚拟地址的最高处。
  // 只有 supervisor 模式在进出用户空间时使用它，所以不设置 PTE_U。
  if(mappages(pagetable, TRAMPOLINE, PGSIZE,
              (uint64)trampoline, PTE_R | PTE_X) < 0){
    uvmfree(pagetable, 0);
    return 0;
  }

  // 为 trampoline.S 将 trapframe 页面映射到 trampoline 页面正下方。
  if(mappages(pagetable, TRAPFRAME, PGSIZE,
              (uint64)(p->trapframe), PTE_R | PTE_W) < 0){
    uvmunmap(pagetable, TRAMPOLINE, 1, 0);
    uvmfree(pagetable, 0);
    return 0;
  }

  return pagetable;
}

// 释放一个进程的页表，以及它引用的物理内存。
void
proc_freepagetable(pagetable_t pagetable, uint64 sz)
{
  uvmunmap(pagetable, TRAMPOLINE, 1, 0);
  uvmunmap(pagetable, TRAPFRAME, 1, 0);
  uvmfree(pagetable, sz);
}

// 一个调用 exec("/init") 的用户程序
// 从 ../user/initcode.S 汇编而来
// od -t xC ../user/initcode
uchar initcode[] = {
  0x17, 0x05, 0x00, 0x00, 0x13, 0x05, 0x45, 0x02,
  0x97, 0x05, 0x00, 0x00, 0x93, 0x85, 0x35, 0x02,
  0x93, 0x08, 0x70, 0x00, 0x73, 0x00, 0x00, 0x00,
  0x93, 0x08, 0x20, 0x00, 0x73, 0x00, 0x00, 0x00,
  0xef, 0xf0, 0x9f, 0xff, 0x2f, 0x69, 0x6e, 0x69,
  0x74, 0x00, 0x00, 0x24, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00
};

// 设置第一个用户进程。
void
userinit(void)
{
  struct proc *p;

  p = allocproc();
  initproc = p;
  
  // 分配一个用户页，并将 initcode 的指令和数据复制进去。
  uvmfirst(p->pagetable, initcode, sizeof(initcode));
  p->sz = PGSIZE;

  // 准备从内核到用户的第一次“返回”。
  p->trapframe->epc = 0;      // 用户程序计数器
  p->trapframe->sp = PGSIZE;  // 用户栈指针

  safestrcpy(p->name, "initcode", sizeof(p->name));
  p->cwd = namei("/");

  p->state = RUNNABLE;

  release(&p->lock);
}

// 增加或减少用户内存 n 字节。
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

// 创建一个新进程，复制父进程。
// 设置子进程的内核栈，使其返回时如同从 fork() 系统调用返回一样。
int
fork(void)
{
  int i, pid;
  struct proc *np;
  struct proc *p = myproc();

  // 分配进程。
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

  // 复制保存的用户寄存器。
  *(np->trapframe) = *(p->trapframe);

  // 使 fork 在子进程中返回 0。
  np->trapframe->a0 = 0;

  // 增加打开文件描述符的引用计数。
  for(i = 0; i < NOFILE; i++)
    if(p->ofile[i])
      np->ofile[i] = filedup(p->ofile[i]);
  np->cwd = idup(p->cwd);

  safestrcpy(np->name, p->name, sizeof(p->name));

  pid = np->pid;

  release(&np->lock);

  acquire(&wait_lock);
  np->parent = p;
  release(&wait_lock);

  acquire(&np->lock);
  np->state = RUNNABLE;
  release(&np->lock);

  return pid;
}

// 将 p 的被遗弃的子进程过继给 init 进程。
// 调用者必须持有 wait_lock。
void
reparent(struct proc *p)
{
  struct proc *pp;

  for(pp = proc; pp < &proc[NPROC]; pp++){
    if(pp->parent == p){
      // 找到一个子进程，将其父进程设置为 initproc
      pp->parent = initproc;
      // 唤醒可能正在 wait 的 initproc
      wakeup(initproc);
    }
  }
}

// 退出当前进程。此函数不会返回。
// 退出的进程会保持在 ZOMBIE 状态，
// 直到其父进程调用 wait()。
void
exit(int status)
{
  struct proc *p = myproc();

  if(p == initproc)
    panic("init exiting");

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

  // 将所有子进程过继给 init。
  reparent(p);

  // 父进程可能在 wait() 中休眠。
  wakeup(p->parent);
  
  acquire(&p->lock);

  p->xstate = status;
  p->state = ZOMBIE;

  release(&wait_lock);

  // 跳转到调度器，永不返回。
  sched();
  panic("zombie exit");
}

// 等待一个子进程退出并返回其 pid。
// 如果该进程没有子进程，则返回 -1。
int
wait(uint64 addr)
{
  struct proc *pp;
  int havekids, pid;
  struct proc *p = myproc();

  acquire(&wait_lock);

  for(;;){
    // 扫描表以查找退出的子进程。
    havekids = 0;
    for(pp = proc; pp < &proc[NPROC]; pp++){
      if(pp->parent == p){
        // 确保子进程不是仍在 exit() 或 swtch() 中。
        acquire(&pp->lock);

        havekids = 1;
        if(pp->state == ZOMBIE){
          // 找到了一个僵尸子进程。
          pid = pp->pid;
          if(addr != 0 && copyout(p->pagetable, addr, (char *)&pp->xstate,
                                  sizeof(pp->xstate)) < 0) {
            release(&pp->lock);
            release(&wait_lock);
            return -1;
          }
          freeproc(pp);
          release(&pp->lock);
          release(&wait_lock);
          return pid;
        }
        release(&pp->lock);
      }
    }

    // 如果我们没有任何子进程，或者当前进程被杀死，等待就没有意义了。
    if(!havekids || killed(p)){
      release(&wait_lock);
      return -1;
    }
    
    // 等待一个子进程退出。
    sleep(p, &wait_lock);  //DOC: wait-sleep
  }
}

// 每个CPU的进程调度器。
// 每个CPU在设置好自己后都会调用 scheduler()。
// 调度器永不返回。它循环执行以下操作：
//  - 选择一个进程来运行。
//  - swtch 以开始运行该进程。
//  - 最终该进程通过 swtch 将控制权交还给调度器。
void
scheduler(void)
{
  struct proc *p;
  struct cpu *c = mycpu();
  
  c->proc = 0;
  for(;;){
    // 最近运行的进程可能已经关闭了中断；
    // 重新启用它们，以避免在所有进程都在等待时发生死锁。
    intr_on();

    int found = 0;
    for(p = proc; p < &proc[NPROC]; p++) {
      acquire(&p->lock);
      if(p->state == RUNNABLE) {
        // 切换到选定的进程。进程的工作是
        // 释放它的锁，然后在跳回我们这里之前
        // 重新获取它。
        p->state = RUNNING;
        c->proc = p;
        swtch(&c->context, &p->context);

        // 进程暂时运行完毕。
        // 它应该在回来之前改变了它的 p->state。
        c->proc = 0;
        found = 1;
      }
      release(&p->lock);
    }
    if(found == 0) {
      // 没有可运行的进程；在此核心上停止运行，直到发生中断。
      intr_on();
      asm volatile("wfi"); // 等待中断
    }
  }
}

// 切换到调度程序。必须只持有 p->lock
// 并且已经更改了 proc->state。保存和恢复
// intena，因为 intena 是这个
// 内核线程的属性，而不是这个 CPU 的。它应该
// 是 proc->intena 和 proc->noff，但这会
// 在少数持有锁但
// 没有进程的情况下出现问题。
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

// 让出CPU一个调度回合。
void
yield(void)
{
  struct proc *p = myproc();
  acquire(&p->lock);
  p->state = RUNNABLE;
  sched();
  release(&p->lock);
}

// fork 子进程第一次被 scheduler() 调度时，
// 会 切换 到 forkret。
void
forkret(void)
{
  static int first = 1;

  // 仍然持有从调度器那里得到的 p->lock。
  release(&myproc()->lock);

  if (first) {
    // 文件系统初始化必须在常规进程的上下文中运行
    //（例如，因为它调用 sleep），因此不能从 main() 运行。
    fsinit(ROOTDEV);

    first = 0;
    // 确保其他核心能看到 first=0。
    __sync_synchronize();
  }

  usertrapret();
}

// 原子地释放锁并在 chan 上休眠。
// 被唤醒时重新获取锁。
void
sleep(void *chan, struct spinlock *lk)
{
  struct proc *p = myproc();
  
  // 必须获取 p->lock 才能
  // 改变 p->state 然后调用 sched。
  // 一旦我们持有 p->lock，我们就可以保证
  // 不会错过任何唤醒（wakeup 会锁定 p->lock），
  // 所以释放 lk 是可以的。

  acquire(&p->lock);  //DOC: sleeplock1
  release(lk);

  // 进入休眠状态。
  p->chan = chan;
  p->state = SLEEPING;

  sched();

  // 清理。
  p->chan = 0;

  // 重新获取原来的锁。
  release(&p->lock);
  acquire(lk);
}

// 唤醒所有在 chan 上休眠的进程。
// 调用时不能持有任何 p->lock。
void
wakeup(void *chan)
{
  struct proc *p;

  for(p = proc; p < &proc[NPROC]; p++) {
    if(p != myproc()){
      acquire(&p->lock);
      if(p->state == SLEEPING && p->chan == chan) {
        p->state = RUNNABLE;
      }
      release(&p->lock);
    }
  }
}

// 杀死给定 pid 的进程。
// 受害者在尝试返回用户空间之前不会退出
// (见 trap.c 中的 usertrap())。
int
kill(int pid)
{
  struct proc *p;

  for(p = proc; p < &proc[NPROC]; p++){
    acquire(&p->lock);
    if(p->pid == pid){
      p->killed = 1;
      if(p->state == SLEEPING){
        // 从 sleep() 中唤醒进程。
        p->state = RUNNABLE;
      }
      release(&p->lock);
      return 0;
    }
    release(&p->lock);
  }
  return -1;
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

// 根据 usr_dst 的值，复制到用户地址或内核地址。
// 成功返回 0，错误返回 -1。
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

// 根据 usr_src 的值，从用户地址或内核地址复制。
// 成功返回 0，错误返回 -1。
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

// 向控制台打印进程列表。用于调试。
// 当用户在控制台输入 ^P 时运行。
// 没有锁，以避免让卡住的机器进一步卡死。
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
