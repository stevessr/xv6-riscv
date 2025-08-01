#ifndef __ASSEMBLER__

// in-line assembly function to read mhartid register, (machine heart id)
// a hart is a hardware thread.
// r_mhartid() returns the id of the calling hart.
// mhartid 是 RISC-V 中的一个标准 CSR（Control and Status Register，控制和状态寄存器）
// 它是一个只读寄存器，包含了正在执行代码的硬件线程（hart）的唯一标识符。
// 在多核系统中，每个核心（core）或硬件线程都有一个唯一的 hart ID。
// xv6 使用这个 ID 来区分不同的 CPU 核心，并为每个核心维护一个独立的 cpu 结构。
static inline uint64
r_mhartid()
{
  uint64 x;
  // "csrr %0, mhartid" 是一个 RISC-V 汇编指令，
  // csrr (Control and Status Register Read) 用于读取 CSR 的值。
  // %0 是一个占位符，表示第一个输出操作数，即变量 x。
  // mhartid 是要读取的 CSR 的名称。
  // "=r" (x) 是 GCC 的内联汇编语法，表示：
  // =: 这是一个输出操作数。
  // r: 将 CSR 的值放入一个通用寄存器（general-purpose register）。
  // (x): 对应的 C 变量是 x。
  // 这条指令的含义是：读取 mhartid 寄存器的值，并将其存入 C 变量 x 中。
  asm volatile("csrr %0, mhartid" : "=r" (x) );
  return x;
}

// Machine Status Register, mstatus
// mstatus 寄存器保存了当前 hart 的状态信息，包括当前的特权级别、中断使能状态等。
// 它是机器模式（M-mode）下最重要的 CSR 之一。

// MSTATUS_MPP_MASK (Machine Previous Privilege mode Mask)
// 用于屏蔽 mstatus 寄存器中除 MPP（Machine Previous Privilege）位之外的其他位。
// MPP 字段（位于 11-12 位）记录了在进入 M-mode 之前 hart 所处的特权级别。
#define MSTATUS_MPP_MASK (3L << 11) // Previous privilege mode.

// MSTATUS_MPP_M (Machine mode)
// 当 MPP 字段为 3（二进制 11）时，表示之前的特权级别是机器模式。
#define MSTATUS_MPP_M    (3L << 11)

// MSTATUS_MPP_S (Supervisor mode)
// 当 MPP 字段为 1（二进制 01）时，表示之前的特权级别是监督者模式。
#define MSTATUS_MPP_S    (1L << 11)

// MSTATUS_MPP_U (User mode)
// 当 MPP 字段为 0（二进制 00）时，表示之前的特权级别是用户模式。
#define MSTATUS_MPP_U    (0L << 11)

// MSTATUS_MIE (Machine Interrupt Enable)
// MIE 位（位于第 3 位）是全局中断使能位。
// 当 MIE = 1 时，M-mode 的中断被使能。
// 当 MIE = 0 时，M-mode 的中断被禁用。
// 这个位会影响所有特权级别的中断。
#define MSTATUS_MIE      (1L << 3)  // Machine-mode Interrupt Enable.

// r_mstatus() 用于读取 mstatus 寄存器的当前值。
static inline uint64
r_mstatus()
{
  uint64 x;
  asm volatile("csrr %0, mstatus" : "=r" (x) );
  return x;
}

// w_mstatus() 用于向 mstatus 寄存器写入一个新值。
// "csrw mstatus, %0" (Control and Status Register Write)
// 将 %0（即 C 变量 x）的值写入 mstatus 寄存器。
// "r" (x) 表示输入操作数 x 是一个通用寄存器。
static inline void 
w_mstatus(uint64 x)
{
  asm volatile("csrw mstatus, %0" : : "r" (x));
}

// Machine Exception Program Counter, mepc
// mepc 寄存器保存了当发生异常或中断并陷入到 M-mode 时，
// 被中断的指令的地址。当使用 mret 指令从 M-mode 返回时，
// CPU 会跳转到 mepc 中保存的地址继续执行。
static inline void 
w_mepc(uint64 x)
{
  asm volatile("csrw mepc, %0" : : "r" (x));
}

// Supervisor Status Register, sstatus
// sstatus 是 mstatus 寄存器的一个子集，包含了在监督者模式（S-mode）下
// 可以访问和修改的状态位。

// SSTATUS_SPP (Supervisor Previous Privilege mode)
// SPP 位（位于第 8 位）记录了在进入 S-mode 之前 hart 所处的特权级别。
// 1 表示监督者模式，0 表示用户模式。
#define SSTATUS_SPP      (1L << 8)  // Previous privilege mode, 1=Supervisor, 0=User

// SSTATUS_SPIE (Supervisor Previous Interrupt Enable)
// SPIE 位（位于第 5 位）保存了在陷入 S-mode 之前 SIE（Supervisor Interrupt Enable）位的值。
// 当从 S-mode 返回时（通过 sret 指令），SIE 位会恢复为 SPIE 的值，
// 并且 SPIE 位会被置为 1。
#define SSTATUS_SPIE     (1L << 5) // Supervisor Previous Interrupt Enable

// SSTATUS_UPIE (User Previous Interrupt Enable)
// UPIE 位（位于第 4 位）在 xv6 中未使用。它用于支持用户模式的中断。
#define SSTATUS_UPIE     (1L << 4) // User Previous Interrupt Enable

// SSTATUS_SIE (Supervisor Interrupt Enable)
// SIE 位（位于第 1 位）控制 S-mode 下的中断是否使能。
// 当 SIE = 1 时，S-mode 的中断被使能。
// 当 SIE = 0 时，S-mode 的中断被禁用。
#define SSTATUS_SIE      (1L << 1)  // Supervisor Interrupt Enable

// SSTATUS_UIE (User Interrupt Enable)
// UIE 位（位于第 0 位）在 xv6 中未使用。它用于控制用户模式的中断是否使能。
#define SSTATUS_UIE      (1L << 0)  // User Interrupt Enable

// r_sstatus() 读取 sstatus 寄存器的值。
static inline uint64
r_sstatus()
{
  uint64 x;
  asm volatile("csrr %0, sstatus" : "=r" (x) );
  return x;
}

// w_sstatus() 向 sstatus 寄存器写入一个新值。
static inline void 
w_sstatus(uint64 x)
{
  asm volatile("csrw sstatus, %0" : : "r" (x));
}

// Supervisor Interrupt Pending
// sip 寄存器记录了当前有哪些 S-mode 的中断正在挂起（等待处理）。
static inline uint64
r_sip()
{
  uint64 x;
  asm volatile("csrr %0, sip" : "=r" (x) );
  return x;
}

// w_sip() 向 sip 寄存器写入一个新值。
// 通常用于清除已处理的中断挂起位。
static inline void 
w_sip(uint64 x)
{
  asm volatile("csrw sip, %0" : : "r" (x));
}

// Supervisor Interrupt Enable
// sie 寄存器用于控制哪些类型的 S-mode 中断是使能的。
// 每一位对应一种中断类型。

// SIE_SEIE (Supervisor External Interrupt Enable)
// 使能来自 PLIC（Platform-Level Interrupt Controller）的外部中断。
#define SIE_SEIE         (1L << 9) // external

// SIE_STIE (Supervisor Timer Interrupt Enable)
// 使能时钟中断。
#define SIE_STIE         (1L << 5) // timer

// SIE_SSIE (Supervisor Software Interrupt Enable)
// 使能由软件触发的中断（用于核间通信）。
#define SIE_SSIE         (1L << 1) // software

// r_sie() 读取 sie 寄存器的值。
static inline uint64
r_sie()
{
  uint64 x;
  asm volatile("csrr %0, sie" : "=r" (x) );
  return x;
}

// w_sie() 向 sie 寄存器写入一个新值。
static inline void 
w_sie(uint64 x)
{
  asm volatile("csrw sie, %0" : : "r" (x));
}

// Machine-mode Interrupt Enable
// mie 寄存器用于控制 M-mode 下各种中断的使能。
#define MIE_STIE (1L << 5) // supervisor timer interrupt-enable
// r_mie() 读取 mie 寄存器的值。
static inline uint64
r_mie()
{
  uint64 x;
  asm volatile("csrr %0, mie" : "=r" (x) );
  return x;
}

// w_mie() 向 mie 寄存器写入一个新值。
static inline void 
w_mie(uint64 x)
{
  asm volatile("csrw mie, %0" : : "r" (x));
}

// Supervisor Exception Program Counter, sepc
// sepc 寄存器保存了当发生异常或中断并陷入到 S-mode 时，
// 被中断的指令的地址。当使用 sret 指令从 S-mode 返回时，
// CPU 会跳转到 sepc 中保存的地址继续执行。
static inline void 
w_sepc(uint64 x)
{
  asm volatile("csrw sepc, %0" : : "r" (x));
}

// r_sepc() 读取 sepc 寄存器的值。
static inline uint64
r_sepc()
{
  uint64 x;
  asm volatile("csrr %0, sepc" : "=r" (x) );
  return x;
}

// Machine Exception Delegation Register
// medeleg 寄存器用于将特定类型的异常从 M-mode 委托给 S-mode 处理。
// 如果 medeleg 的某一位被置 1，那么对应类型的异常发生时，
// 控制权将直接转移到 S-mode 的陷阱处理程序，而不是 M-mode。
static inline uint64
r_medeleg()
{
  uint64 x;
  asm volatile("csrr %0, medeleg" : "=r" (x) );
  return x;
}

// w_medeleg() 向 medeleg 寄存器写入一个新值。
static inline void 
w_medeleg(uint64 x)
{
  asm volatile("csrw medeleg, %0" : : "r" (x));
}

// Machine Interrupt Delegation Register
// mideleg 寄存器用于将特定类型的中断从 M-mode 委托给 S-mode 处理。
// 工作原理与 medeleg 类似。xv6 将所有 S-mode 相关中断都委托给 S-mode。
static inline uint64
r_mideleg()
{
  uint64 x;
  asm volatile("csrr %0, mideleg" : "=r" (x) );
  return x;
}

// w_mideleg() 向 mideleg 寄存器写入一个新值。
static inline void 
w_mideleg(uint64 x)
{
  asm volatile("csrw mideleg, %0" : : "r" (x));
}

// Supervisor Trap Vector Base Address Register
// stvec 寄存器保存了 S-mode 陷阱处理程序的基地址。
// 当在 S-mode 或 U-mode 发生陷阱（异常或中断）时，
// CPU 会跳转到 stvec 指向的地址开始执行。
static inline void 
w_stvec(uint64 x)
{
  asm volatile("csrw stvec, %0" : : "r" (x));
}

// r_stvec() 读取 stvec 寄存器的值。
static inline uint64
r_stvec()
{
  uint64 x;
  asm volatile("csrr %0, stvec" : "=r" (x) );
  return x;
}

// Supervisor-mode Timer Compare Register
// stimecmp 是一个虚拟的 CSR，用于设置下一次时钟中断的时间点。
// 当内部时钟计数器 time 的值达到或超过 stimecmp 的值时，
// 会触发一个时钟中断。
// 由于 QEMU 的一个 bug，不能直接使用 "csrr %0, stimecmp"，
// 而是需要使用它的地址 0x14d。
static inline uint64
r_stimecmp()
{
  uint64 x;
  // asm volatile("csrr %0, stimecmp" : "=r" (x) );
  asm volatile("csrr %0, 0x14d" : "=r" (x) );
  return x;
}

// w_stimecmp() 向 stimecmp 寄存器写入一个新值，
// 通常是当前时间加上一个固定的时间间隔。
static inline void 
w_stimecmp(uint64 x)
{
  // asm volatile("csrw stimecmp, %0" : : "r" (x));
  asm volatile("csrw 0x14d, %0" : : "r" (x));
}

// Machine-mode Environment Configuration Register
// menvcfg 寄存器用于配置机器环境。
// 在 QEMU virt 机器上，它的 FIOM 位（bit 0）控制了 MMIO（内存映射 I/O）
// 访问是否会经过内存管理单元（MMU）。
// xv6 将其置 1，以确保对 PLIC 等设备的访问不会被 MMU 转换。
static inline uint64
r_menvcfg()
{
  uint64 x;
  asm volatile("csrr %0, 0x30a" : "=r" (x) );
  return x;
}

static inline void 
w_menvcfg(uint64 x)
{
  asm volatile("csrw 0x30a, %0" : : "r" (x));
}

// Physical Memory Protection
// PMP 相关的 CSR 用于配置物理内存保护单元，
// 允许 M-mode 对物理内存的访问权限进行精细控制。
// xv6 在启动时使用 PMP 来设置初始的内存权限。
static inline void
w_pmpcfg0(uint64 x)
{
  asm volatile("csrw pmpcfg0, %0" : : "r" (x));
}

static inline void
w_pmpaddr0(uint64 x)
{
  asm volatile("csrw pmpaddr0, %0" : : "r" (x));
}

// use riscv's sv39 page table scheme.
// SATP_SV39 表示使用 Sv39 分页模式。
// 在 Sv39 模式下，虚拟地址是 39 位，分为三级页表索引（每级 9 位）
// 和一个页内偏移（12 位）。
#define SATP_SV39 (8L << 60)

// MAKE_SATP 宏用于构建 satp 寄存器的值。
// 它将页表的物理地址右移 12 位（除以 4096），
// 然后与 Sv39 模式标识符进行或运算。
#define MAKE_SATP(pagetable) (SATP_SV39 | (((uint64)pagetable) >> 12))

// Supervisor Address Translation and Protection;
// holds the address of the page table.
// satp 寄存器用于启用分页机制并指定根页表的物理地址。
static inline void
w_satp(uint64 x)
{
  asm volatile("csrw satp, %0" : : "r" (x));
}

static inline uint64
r_satp()
{
  uint64 x;
  asm volatile("csrr %0, satp" : "=r" (x) );
  return x;
}

// Supervisor Trap Cause
// scause 寄存器记录了导致陷阱的原因。
// 最高位表示是中断（1）还是异常（0），
// 低位则表示具体的异常或中断类型代码。
static inline uint64
r_scause()
{
  uint64 x;
  asm volatile("csrr %0, scause" : "=r" (x) );
  return x;
}

// Supervisor Trap Value
// stval 寄存器保存了与陷阱相关的附加信息。
// 例如，在缺页异常时，它会保存导致异常的虚拟地址。
static inline uint64
r_stval()
{
  uint64 x;
  asm volatile("csrr %0, stval" : "=r" (x) );
  return x;
}

// Machine-mode Counter-Enable Register
// mcounteren 寄存器用于控制在 S-mode 或 U-mode 下
// 是否可以访问 time 等计数器寄存器。
static inline void
w_mcounteren(uint64 x)
{
  asm volatile("csrw mcounteren, %0" : : "r" (x));
}

static inline uint64
r_mcounteren()
{
  uint64 x;
  asm volatile("csrr %0, mcounteren" : "=r" (x) );
  return x;
}

// machine-mode cycle counter
// time 寄存器是一个 64 位的计数器，以固定的频率递增。
// 通常用作系统的实时时钟源。
static inline uint64
r_time()
{
  uint64 x;
  // "rdtime %0" 是一个伪指令，实际会被汇编器展开为
  // csrr %0, time
  asm volatile("csrr %0, time" : "=r" (x) );
  return x;
}

// enable device interrupts
// intr_on() 通过设置 sstatus 寄存器中的 SIE 位来使能 S-mode 的中断。
static inline void
intr_on()
{
  w_sstatus(r_sstatus() | SSTATUS_SIE);
}

// disable device interrupts
// intr_off() 通过清除 sstatus 寄存器中的 SIE 位来禁用 S-mode 的中断。
static inline void
intr_off()
{
  w_sstatus(r_sstatus() & ~SSTATUS_SIE);
}

// are device interrupts enabled?
// intr_get() 检查 sstatus 寄存器中的 SIE 位，返回中断是否使能。
static inline int
intr_get()
{
  uint64 x = r_sstatus();
  return (x & SSTATUS_SIE) != 0;
}

// r_sp() 读取栈指针（Stack Pointer）寄存器 sp 的值。
static inline uint64
r_sp()
{
  uint64 x;
  asm volatile("mv %0, sp" : "=r" (x) );
  return x;
}

// read and write tp, the thread pointer, which holds
// this core's hartid (core number), the index into cpus[].
// 在 xv6 中，tp (Thread Pointer) 寄存器被用来保存当前 CPU 核心的 hartid。
// 这是一个 xv6 特定的约定，用于在多核环境中快速获取当前核心的 ID。
static inline uint64
r_tp()
{
  uint64 x;
  asm volatile("mv %0, tp" : "=r" (x) );
  return x;
}

// w_tp() 向 tp 寄存器写入一个新值。
static inline void 
w_tp(uint64 x)
{
  asm volatile("mv tp, %0" : : "r" (x));
}

// r_ra() 读取返回地址（Return Address）寄存器 ra 的值。
// ra 寄存器保存了函数调用后应该返回的地址。
static inline uint64
r_ra()
{
  uint64 x;
  asm volatile("mv %0, ra" : "=r" (x) );
  return x;
}

// flush the TLB.
// sfence.vma (Supervisor-mode Fence for Virtual Memory Address)
// 这条指令用于刷新 TLB（Translation Lookaside Buffer，地址转换后备缓冲）。
// 当页表内容被修改后，需要执行 sfence.vma 来确保 CPU 使用最新的页表项
// 进行地址转换，而不是使用 TLB 中缓存的旧条目。
// "zero, zero" 表示刷新所有地址空间的 TLB 条目。
static inline void
sfence_vma()
{
  // the zero, zero means flush all TLB entries.
  asm volatile("sfence.vma zero, zero");
}

// pte_t 是页表项（Page Table Entry）的类型定义。
typedef uint64 pte_t;
// pagetable_t 是页表的类型定义，它是一个指向 pte_t 的指针。
// 一个页表包含 512 个页表项。
typedef uint64 *pagetable_t; // 512 PTEs

#endif // __ASSEMBLER__

// PGSIZE 定义了一页的大小，在 RISC-V 中通常是 4096 字节。
#define PGSIZE 4096 // bytes per page
// PGSHIFT 是页内偏移的位数，2^12 = 4096。
#define PGSHIFT 12  // bits of offset within a page

// PGROUNDUP(sz) 将一个大小 sz 向上取整到最近的页边界。
// 例如，PGROUNDUP(4097) = 8192。
#define PGROUNDUP(sz)  (((sz)+PGSIZE-1) & ~(PGSIZE-1))
// PGROUNDDOWN(a) 将一个地址 a 向下取整到最近的页边界。
// 例如，PGROUNDDOWN(8191) = 4096。
#define PGROUNDDOWN(a) (((a)) & ~(PGSIZE-1))

// Page Table Entry (PTE) flags
// PTE 的低 10 位是标志位，用于描述页的属性。

// PTE_V (Valid)
// 如果该位置 1，表示这个 PTE 是有效的，可以用于地址转换。
#define PTE_V (1L << 0)

// PTE_R (Readable)
// 如果该位置 1，表示该页是可读的。
#define PTE_R (1L << 1)

// PTE_W (Writable)
// 如果该位置 1，表示该页是可写的。
#define PTE_W (1L << 2)

// PTE_X (Executable)
// 如果该位置 1，表示该页是可执行的（即可以从中取指令）。
#define PTE_X (1L << 3)

// PTE_U (User)
// 如果该位置 1，表示用户模式（U-mode）可以访问该页。
// 如果为 0，则只有监督者模式（S-mode）可以访问。
#define PTE_U (1L << 4) // 1 -> user can access

// Shift a physical address to the right place for a PTE.
// PA2PTE() 将物理地址（PA）转换为 PTE 中的物理页号（PPN）部分。
// 物理地址先右移 12 位（去掉页内偏移），然后左移 10 位（为标志位留出空间）。
#define PA2PTE(pa) ((((uint64)pa) >> 12) << 10)

// Shift a PTE to a physical address.
// PTE2PA() 将 PTE 中的 PPN 部分转换为物理地址。
// PTE 先右移 10 位（去掉标志位），然后左移 12 位（补上页内偏移）。
#define PTE2PA(pte) (((pte) >> 10) << 12)

// PTE_FLAGS() 提取 PTE 中的标志位（低 10 位）。
#define PTE_FLAGS(pte) ((pte) & 0x3FF)

// Extract the three 9-bit page table indices from a virtual address.
// 在 Sv39 分页模式下，39 位的虚拟地址被划分为：
// 38-30: 2 级页表索引 (L2)
// 29-21: 1 级页表索引 (L1)
// 20-12: 0 级页表索引 (L0)
// 11-0:  页内偏移

// PXMASK 是一个 9 位的掩码（0x1FF），用于提取索引。
#define PXMASK          0x1FF // 9-bit mask.
// PXSHIFT(level) 计算指定级别页表索引的起始位。
#define PXSHIFT(level)  (PGSHIFT + (9 * (level)))
// PX(level, va) 从虚拟地址 va 中提取指定级别 level 的页表索引。
#define PX(level, va) ((((uint64) (va)) >> PXSHIFT(level)) & PXMASK)

// one beyond the highest possible virtual address.
// MAXVA 定义了 xv6 支持的最大虚拟地址。
// Sv39 的虚拟地址空间是 2^39，但 xv6 将其限制为 2^38，
// 以避免处理符号扩展带来的复杂性。
#define MAXVA (1L << (9 + 9 + 9 + 12 - 1))
