#- * - coding : utf - 8 - * -
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

extern char trampoline[]; // trampoline.S

// 帮助确保 wait() 的父进程的唤醒不会丢失。
// 在使用 p->parent 时帮助遵守内存模型。
// 必须在任何 p->lock 之前获取。
struct spinlock wait_lock;

// 为每个进程的内核栈分配一个页面。
// 将其映射到高内存地址，后跟一个无效的保护页面。
void proc_mapstacks(pagetable_t kpgtbl)
{
  struct proc *p;

  for (p = proc; p < &proc[NPROC]; p++)
  {
    char *pa = kalloc();
    if (pa == 0)
      panic("kalloc");
    uint64 va = KSTACK((int)(p - proc));
    kvmmap(kpgtbl, va, (uint64)pa, PGSIZE, PTE_R | PTE_W);
  }
}

// 初始化进程表。
void procinit(void)
{
  struct proc *p;

  initlock(&pid_lock, "nextpid");
  initlock(&wait_lock, "wait_lock");
  for (p = proc; p < &proc[NPROC]; p++)
  {
    initlock(&p->lock, "proc");
    p->state = UNUSED;
    p->kstack = KSTACK((int)(p - proc));
  }
}

// 必须在禁用中断的情况下调用，
// 以防止与移动到不同CPU的进程发生竞争。
int cpuid()
{
  int id = r_tp();
  return id;
}

// 返回此CPU的cpu结构。
// 必须禁用中断。
struct cpu *
mycpu(void)
{
  int id = cpuid();
  struct cpu *c = &cpus[id];
  return c;
}

// 返回当前的proc结构指针，如果没有则返回零。
struct proc *
myproc(void)
{
  push_off();
  struct cpu *c = mycpu();
  struct proc *p = c->proc;
  pop_off();
  return p;
}

int allocpid()
{
  int pid;

  acquire(&pid_lock);
  pid = nextpid;
  nextpid = nextpid + 1;
  release(&pid_lock);

  return pid;
}

// 在进程表中查找一个UNUSED的进程。
// 如果找到，初始化在内核中运行所需的状态，
// 并返回时持有p->lock。
// 如果没有空闲的进程，或者内存分配失败，则返回0。
static struct proc *
allocproc(void)
{
  struct proc *p;

  for (p = proc; p < &proc[NPROC]; p++)
  {
    acquire(&p->lock);
    if (p->state == UNUSED)
    {
      goto found;
    }
    else
    {
      release(&p->lock);
    }
  }
  return 0;

found:
  p->pid = allocpid();
  p->state = USED;

  // 分配一个trapframe页。
  if ((p->trapframe = (struct trapframe *)kalloc()) == 0)
  {
    freeproc(p);
    release(&p->lock);
    return 0;
  }

  // 一个空的用户页表。
  p->pagetable = proc_pagetable(p);
  if (p->pagetable == 0)
  {
    freeproc(p);
    release(&p->lock);
    return 0;
  }

  // 设置新的上下文以在forkret处开始执行，
  // forkret返回到用户空间。
  memset(&p->context, 0, sizeof(p->context));
  p->context.ra = (uint64)forkret;
  p->context.sp = p->kstack + PGSIZE;

  return p;
}

// 释放一个proc结构及其附属数据，
// 包括用户页。
// 必须持有p->lock。
static void
freeproc(struct proc *p)
{
  if (p->trapframe)
    kfree((void *)p->trapframe);
  p->trapframe = 0;
  if (p->pagetable)
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

// 为给定进程创建一个用户页表，没有用户内存，
// 但有trampoline和trapframe页。
pagetable_t
proc_pagetable(struct proc *p)
{
  pagetable_t pagetable;

  // 一个空的页表。
  pagetable = uvmcreate();
  if (pagetable == 0)
    return 0;

  // 在最高的用户虚拟地址映射trampoline代码（用于系统调用返回）。
  // 只有supervisor在进出用户空间时使用它，所以没有PTE_U。
  if (mappages(pagetable, TRAMPOLINE, PGSIZE,
               (uint64)trampoline, PTE_R | PTE_X) < 0)
  {
    uvmfree(pagetable, 0);
    return 0;
  }

  // 在trampoline页下面映射trapframe页，供trampoline.S使用。
  if (mappages(pagetable, TRAPFRAME, PGSIZE,
               (uint64)(p->trapframe), PTE_R | PTE_W) < 0)
  {
    uvmunmap(pagetable, TRAMPOLINE, 1, 0);
    uvmfree(pagetable, 0);
    return 0;
  }

  return pagetable;
}

// 释放一个进程的页表，并释放它引用的物理内存。
void proc_freepagetable(pagetable_t pagetable, uint64 sz)
{
  uvmunmap(pagetable, TRAMPOLINE, 1, 0);
  uvmunmap(pagetable, TRAPFRAME, 1, 0);
  uvmfree(pagetable, sz);
}

// 设置第一个用户进程。
void userinit(void)
{
  struct proc *p;

  p = allocproc();
  initproc = p;

  p->cwd = namei("/");

  p->state = RUNNABLE;

  release(&p->lock);
}

// 将用户内存缩小n字节。
// 成功返回0，失败返回-1。
int growproc(int n)
{
  uint64 sz;
  struct proc *p = myproc();

  sz = p->sz;
  if (n > 0)
  {
    if ((sz = uvmalloc(p->pagetable, sz, sz + n, PTE_W)) == 0)
    {
      return -1;
    }
  }
  else if (n < 0)
  {
    sz = uvmdealloc(p->pagetable, sz, sz + n);
  }
  p->sz = sz;
  return 0;
}

// 创建一个新进程，复制父进程。
// 设置子进程内核栈，使其返回时如同从fork()系统调用返回一样。
int kfork(void)
{
  int i, pid;
  struct proc *np;
  struct proc *p = myproc();

  // 分配进程。
  if ((np = allocproc()) == 0)
  {
    return -1;
  }

  // 从父进程复制用户内存到子进程。
  if (uvmcopy(p->pagetable, np->pagetable, p->sz) < 0)
  {
    freeproc(np);
    release(&np->lock);
    return -1;
  }
  np->sz = p->sz;

  // 复制保存的用户寄存器。
  *(np->trapframe) = *(p->trapframe);

  // 使fork在子进程中返回0。
  np->trapframe->a0 = 0;

  // 增加打开文件描述符的引用计数。
  for (i = 0; i < NOFILE; i++)
    if (p->ofile[i])
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

// 将p的被遗弃的子进程交给init。
// 调用者必须持有wait_lock。
void reparent(struct proc *p)
{
  struct proc *pp;

  for (pp = proc; pp < &proc[NPROC]; pp++)
  {
    if (pp->parent == p)
    {
      pp->parent = initproc;
      wakeup(initproc);
    }
  }
}

// 退出当前进程。不会返回。
// 退出的进程保持在僵尸状态，直到其父进程调用wait()。
void kexit(int status)
{
  struct proc *p = myproc();

  if (p == initproc)
    panic("init exiting");

  // 关闭所有打开的文件。
  for (int fd = 0; fd < NOFILE; fd++)
  {
    if (p->ofile[fd])
    {
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

  // 将任何子进程交给init。
  reparent(p);

  // 父进程可能在wait()中休眠。
  wakeup(p->parent);

  acquire(&p->lock);

  p->xstate = status;
  p->state = ZOMBIE;

  release(&wait_lock);

  // 跳转到调度程序，永不返回。
  sched();
  panic("zombie exit");
}

// 等待一个子进程退出并返回其pid。
// 如果此进程没有子进程，则返回-1。
int kwait(uint64 addr)
{
  struct proc *pp;
  int havekids, pid;
  struct proc *p = myproc();

  acquire(&wait_lock);

  for (;;)
  {
    // 扫描表以查找退出的子进程。
    havekids = 0;
    for (pp = proc; pp < &proc[NPROC]; pp++)
    {
      if (pp->parent == p)
      {
        // 确保子进程不在exit()或swtch()中。
        acquire(&pp->lock);

        havekids = 1;
        if (pp->state == ZOMBIE)
        {
          // 找到了一个。
          pid = pp->pid;
          if (addr != 0 && copyout(p->pagetable, addr, (char *)&pp->xstate,
                                   sizeof(pp->xstate)) < 0)
          {
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

    // 如果我们没有任何子进程，等待就没有意义了。
    if (!havekids || killed(p))
    {
      release(&wait_lock);
      return -1;
    }

    // 等待一个子进程退出。
    sleep(p, &wait_lock);
  }
}

// 每CPU的进程调度程序。
// 每个CPU在设置好自己后调用scheduler()。
// Scheduler从不返回。它循环执行：
//  - 选择一个要运行的进程。
//  - swtch以开始运行该进程。
//  - 最终该进程通过swtch将控制权交还给调度程序。
void scheduler(void)
{
  struct proc *p;
  struct cpu *c = mycpu();

  c->proc = 0;
  for (;;)
  {
    // 最近运行的进程可能已关闭中断；
    // 启用它们以避免在所有进程都在等待时出现死锁。
    // 然后再次关闭它们以避免中断和wfi之间可能出现的竞争。
    intr_on();
    intr_off();

    int found = 0;
    for (p = proc; p < &proc[NPROC]; p++)
    {
      acquire(&p->lock);
      if (p->state == RUNNABLE)
      {
        // 切换到选定的进程。进程的工作是
        // 释放它的锁，然后在跳回我们这里之前重新获取它。
        p->state = RUNNING;
        c->proc = p;
        swtch(&c->context, &p->context);

        // 进程暂时运行完毕。
        // 它应该在回来之前改变了它的p->state。
        c->proc = 0;
        found = 1;
      }
      release(&p->lock);
    }
    if (found == 0)
    {
      // 没有可运行的；在此核心上停止运行直到中断。
      asm volatile("wfi");
    }
  }
}

// 切换到调度程序。必须只持有p->lock
// 并且已经改变了proc->state。保存和恢复
// intena，因为intena是这个内核线程的属性，
// 而不是这个CPU的。它应该是proc->intena和proc->noff，
// 但这会在少数持有锁但没有进程的地方中断。
void sched(void)
{
  int intena;
  struct proc *p = myproc();

  if (!holding(&p->lock))
    panic("sched p->lock");
  if (mycpu()->noff != 1)
    panic("sched locks");
  if (p->state == RUNNING)
    panic("sched RUNNING");
  if (intr_get())
    panic("sched interruptible");

  intena = mycpu()->intena;
  swtch(&p->context, &mycpu()->context);
  mycpu()->intena = intena;
}

// 放弃CPU一个调度周期。
void yield(void)
{
  struct proc *p = myproc();
  acquire(&p->lock);
  p->state = RUNNABLE;
  sched();
  release(&p->lock);
}

// fork子进程的第一次调度由scheduler()
// 将切换到forkret。
void forkret(void)
{
  extern char userret[];
  static int first = 1;
  struct proc *p = myproc();

  // 仍然持有来自调度程序的p->lock。
  release(&p->lock);

  if (first)
  {
    // 文件系统初始化必须在常规进程的上下文中运行
    // （例如，因为它调用sleep），因此不能从main()运行。
    fsinit(ROOTDEV);

    first = 0;
    // 确保其他核心看到first=0。
    __sync_synchronize();

    // 现在文件系统已初始化，我们可以调用kexec()。
    // 将kexec的返回值（argc）放入a0。
    p->trapframe->a0 = kexec("/init", (char *[]){"/init", 0});
    if (p->trapframe->a0 == -1)
    {
      panic("exec");
    }
  }

  // 返回用户空间，模仿usertrap()的返回。
  prepare_return();
  uint64 satp = MAKE_SATP(p->pagetable);
  uint64 trampoline_userret = TRAMPOLINE + (userret - trampoline);
  ((void (*)(uint64))trampoline_userret)(satp);
}

// 在通道chan上休眠，释放条件锁lk。
// 唤醒时重新获取lk。
void sleep(void *chan, struct spinlock *lk)
{
  struct proc *p = myproc();

  // 必须获取p->lock才能更改p->state然后调用sched。
  // 一旦我们持有p->lock，我们就可以保证不会错过任何唤醒
  // （wakeup会锁定p->lock），
  // 所以释放lk是可以的。

  acquire(&p->lock);
  release(lk);

  // 进入睡眠。
  p->chan = chan;
  p->state = SLEEPING;

  sched();

  // 清理。
  p->chan = 0;

  // 重新获取原始锁。
  release(&p->lock);
  acquire(lk);
}

// 唤醒所有在通道chan上休眠的进程。
// 调用者应持有条件锁。
void wakeup(void *chan)
{
  struct proc *p;

  for (p = proc; p < &proc[NPROC]; p++)
  {
    if (p != myproc())
    {
      acquire(&p->lock);
      if (p->state == SLEEPING && p->chan == chan)
      {
        p->state = RUNNABLE;
      }
      release(&p->lock);
    }
  }
}

// 杀死给定pid的进程。
// 受害者在尝试返回用户空间之前不会退出
// （参见trap.c中的usertrap()）。
int kkill(int pid)
{
  struct proc *p;

  for (p = proc; p < &proc[NPROC]; p++)
  {
    acquire(&p->lock);
    if (p->pid == pid)
    {
      p->killed = 1;
      if (p->state == SLEEPING)
      {
        // 从sleep()中唤醒进程。
        p->state = RUNNABLE;
      }
      release(&p->lock);
      return 0;
    }
    release(&p->lock);
  }
  return -1;
}

void setkilled(struct proc *p)
{
  acquire(&p->lock);
  p->killed = 1;
  release(&p->lock);
}

int killed(struct proc *p)
{
  int k;

  acquire(&p->lock);
  k = p->killed;
  release(&p->lock);
  return k;
}

// 根据usr_dst，复制到用户地址或内核地址。
// 成功返回0，错误返回-1。
int either_copyout(int user_dst, uint64 dst, void *src, uint64 len)
{
  struct proc *p = myproc();
  if (user_dst)
  {
    return copyout(p->pagetable, dst, src, len);
  }
  else
  {
    memmove((char *)dst, src, len);
    return 0;
  }
}

// 根据usr_src，从用户地址或内核地址复制。
// 成功返回0，错误返回-1。
int either_copyin(void *dst, int user_src, uint64 src, uint64 len)
{
  struct proc *p = myproc();
  if (user_src)
  {
    return copyin(p->pagetable, dst, src, len);
  }
  else
  {
    memmove(dst, (char *)src, len);
    return 0;
  }
}

// 将进程列表打印到控制台。用于调试。
// 当用户在控制台上键入^P时运行。
// 没有锁以避免进一步卡住卡死的机器。
void procdump(void)
{
  static char *states[] = {
      [UNUSED] "unused",
      [USED] "used",
      [SLEEPING] "sleep ",
      [RUNNABLE] "runble",
      [RUNNING] "run   ",
      [ZOMBIE] "zombie"};
  struct proc *p;
  char *state;

  printf("\n");
  for (p = proc; p < &proc[NPROC]; p++)
  {
    if (p->state == UNUSED)
      continue;
    if (p->state >= 0 && p->state < NELEM(states) && states[p->state])
      state = states[p->state];
    else
      state = "???";
    printf("%d %s %s", p->pid, state, p->name);
    printf("\n");
  }
}