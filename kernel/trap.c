#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "proc.h"
#include "defs.h"

struct spinlock tickslock;
uint ticks;

extern char trampoline[], uservec[], userret[];

// COW helpers in vm.c
extern void increfpa(uint64 pa);
extern void decrefpa(uint64 pa);
// refcount array and lock in vm.c
extern uint refcount[];
extern struct spinlock ref_lock;

// in kernelvec.S, calls kerneltrap().
void kernelvec();

extern int devintr();

void
trapinit(void)
{
  initlock(&tickslock, "time");
}

// set up to take exceptions and traps while in the kernel.
void
trapinithart(void)
{
  w_stvec((uint64)kernelvec);
}

//
// handle an interrupt, exception, or system call from user space.
// called from trampoline.S
//
void
usertrap(void)
{
  int which_dev = 0;

  if((r_sstatus() & SSTATUS_SPP) != 0)
    panic("usertrap: not from user mode");

  // send interrupts and exceptions to kerneltrap(),
  // since we're now in the kernel.
  w_stvec((uint64)kernelvec);

  struct proc *p = myproc();
  
  // save user program counter.
  p->trapframe->epc = r_sepc();
  
  // read cause and fault value once to avoid multiple CSR reads
  uint64 scause = r_scause();
  uint64 stval = r_stval();

  if(scause == 8){
    // system call

    if(killed(p))
      exit(-1);

    // sepc points to the ecall instruction,
    // but we want to return to the next instruction.
    p->trapframe->epc += 4;

    // an interrupt will change sepc, scause, and sstatus,
    // so enable only now that we're done with those registers.
    intr_on();

    syscall();
  } else if((which_dev = devintr()) != 0){
    // ok
  } else {
    // Handle page faults for copy-on-write (store page faults)
    if(scause == 15){ // store page fault on RISC-V
      uint64 va = stval;
      uint64 va0 = PGROUNDDOWN(va);
      // If the faulting address is at or above MAXVA, do not call walk()
      // because walk() panics for va >= MAXVA. Instead, kill the process.
      if(va0 >= MAXVA){
        printf("usertrap(): write to invalid VA 0x%lx >= MAXVA 0x%lx pid=%d\n", va0, (uint64)MAXVA, p->pid);
        setkilled(p);
      } else {
        pte_t *pte = walk(p->pagetable, va0, 0);
        
        if(pte == 0 || (*pte & PTE_V) == 0 || (*pte & PTE_U) == 0){
          printf("usertrap(): unexpected scause 0x%lx pid=%d\n", scause, p->pid);
          printf("            sepc=0x%lx stval=0x%lx\n", p->trapframe->epc, stval);
          setkilled(p);
        } else {
          uint64 pa = PTE2PA(*pte);
          uint flags = PTE_FLAGS(*pte);
          // if writable, shouldn't fault; check if COW page
          if((flags & PTE_COW) && (flags & PTE_W) == 0){
            // This is a COW page
            uint64 idx = (pa - KERNBASE) / PGSIZE;
            // read refcount under lock to avoid races with other harts
            uint cnt;
            acquire(&ref_lock);
            cnt = refcount[idx];
            release(&ref_lock);
            if(cnt > 1){
              // allocate new page and copy
              char *mem = kalloc();
              if(mem == 0){
                setkilled(p);
              } else {
                memmove(mem, (char*)pa, PGSIZE);
                decrefpa(pa);
                increfpa((uint64)mem);
                // Remove COW flag and add write permission back
                flags = (flags & ~PTE_COW) | PTE_W;
                *pte = PA2PTE((uint64)mem) | flags;
                sfence_vma();
              }
            } else if(cnt == 1){
              // Only one reference, just make writable
              flags = (flags & ~PTE_COW) | PTE_W;
              *pte = PA2PTE(pa) | flags;
              sfence_vma();
            } else {
              // shouldn't happen
              printf("usertrap(): COW page with refcount error\n");
              setkilled(p);
            }
          } else {
            // Not a COW page but faulted
            printf("usertrap(): page fault not COW pid=%d\n", p->pid);
            printf("            sepc=0x%lx stval=0x%lx pte=0x%lx\n", p->trapframe->epc, stval, *pte);
            setkilled(p);
          }
        }
      }
    }
  }
  if(killed(p))
    exit(-1);

  // give up the CPU if this is a timer interrupt.
  if(which_dev == 2)
    yield();

  usertrapret();
}

//
// return to user space
//
void
usertrapret(void)
{
  struct proc *p = myproc();

  // we're about to switch the destination of traps from
  // kerneltrap() to usertrap(), so turn off interrupts until
  // we're back in user space, where usertrap() is correct.
  intr_off();

  // send syscalls, interrupts, and exceptions to uservec in trampoline.S
  uint64 trampoline_uservec = TRAMPOLINE + (uservec - trampoline);
  w_stvec(trampoline_uservec);

  // set up trapframe values that uservec will need when
  // the process next traps into the kernel.
  p->trapframe->kernel_satp = r_satp();         // kernel page table
  p->trapframe->kernel_sp = p->kstack + PGSIZE; // process's kernel stack
  p->trapframe->kernel_trap = (uint64)usertrap;
  p->trapframe->kernel_hartid = r_tp();         // hartid for cpuid()

  // set up the registers that trampoline.S's sret will use
  // to get to user space.
  
  // set S Previous Privilege mode to User.
  unsigned long x = r_sstatus();
  x &= ~SSTATUS_SPP; // clear SPP to 0 for user mode
  x |= SSTATUS_SPIE; // enable interrupts in user mode
  w_sstatus(x);

  // set S Exception Program Counter to the saved user pc.
  w_sepc(p->trapframe->epc);

  // tell trampoline.S the user page table to switch to.
  uint64 satp = MAKE_SATP(p->pagetable);

  // jump to userret in trampoline.S at the top of memory, which 
  // switches to the user page table, restores user registers,
  // and switches to user mode with sret.
  uint64 trampoline_userret = TRAMPOLINE + (userret - trampoline);
  ((void (*)(uint64))trampoline_userret)(satp);
}

// interrupts and exceptions from kernel code go here via kernelvec,
// on whatever the current kernel stack is.
void 
kerneltrap()
{
  int which_dev = 0;
  uint64 sepc = r_sepc();
  uint64 sstatus = r_sstatus();
  uint64 scause = r_scause();
  
  if((sstatus & SSTATUS_SPP) == 0)
    panic("kerneltrap: not from supervisor mode");
  if(intr_get() != 0)
    panic("kerneltrap: interrupts enabled");

  if((which_dev = devintr()) == 0){
    // interrupt or trap from an unknown source
    printf("scause=0x%lx sepc=0x%lx stval=0x%lx\n", scause, r_sepc(), r_stval());
    panic("kerneltrap");
  }

  // give up the CPU if this is a timer interrupt.
  if(which_dev == 2 && myproc() != 0)
    yield();

  // the yield() may have caused some traps to occur,
  // so restore trap registers for use by kernelvec.S's sepc instruction.
  w_sepc(sepc);
  w_sstatus(sstatus);
}

void
clockintr()
{
  if(cpuid() == 0){
    acquire(&tickslock);
    ticks++;
    wakeup(&ticks);
    release(&tickslock);
  }

  // ask for the next timer interrupt. this also clears
  // the interrupt request. 1000000 is about a tenth
  // of a second.
  w_stimecmp(r_time() + 1000000);
}

// check if it's an external interrupt or software interrupt,
// and handle it.
// returns 2 if timer interrupt,
// 1 if other device,
// 0 if not recognized.
int
devintr()
{
  uint64 scause = r_scause();

  if(scause == 0x8000000000000009L){
    // this is a supervisor external interrupt, via PLIC.

    // irq indicates which device interrupted.
    int irq = plic_claim();

    if(irq == UART0_IRQ){
      uartintr();
    } else if(irq == VIRTIO0_IRQ){
      virtio_disk_intr();
    } else if(irq){
      printf("unexpected interrupt irq=%d\n", irq);
    }

    // the PLIC allows each device to raise at most one
    // interrupt at a time; tell the PLIC the device is
    // now allowed to interrupt again.
    if(irq)
      plic_complete(irq);

    return 1;
  } else if(scause == 0x8000000000000005L){
    // timer interrupt.
    clockintr();
    return 2;
  } else {
    return 0;
  }
}

