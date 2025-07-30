#ifndef __ASSEMBLER__

// 这是哪个 hart (核心)？
static inline uint64
r_mhartid()
{
  uint64 x;
  asm volatile("csrr %0, mhartid" : "=r" (x) );
  return x;
}

// 机器状态寄存器, mstatus

#define MSTATUS_MPP_MASK (3L << 11) // 先前模式
#define MSTATUS_MPP_M (3L << 11)    // 机器模式
#define MSTATUS_MPP_S (1L << 11)    // 监督者模式
#define MSTATUS_MPP_U (0L << 11)    // 用户模式
#define MSTATUS_MIE (1L << 3)       // 机器模式中断使能

static inline uint64
r_mstatus()
{
  uint64 x;
  asm volatile("csrr %0, mstatus" : "=r" (x) );
  return x;
}

static inline void 
w_mstatus(uint64 x)
{
  asm volatile("csrw mstatus, %0" : : "r" (x));
}

// 机器异常程序计数器，保存异常返回后
// 将要执行的指令地址。
static inline void
w_mepc(uint64 x)
{
  asm volatile("csrw mepc, %0" : : "r" (x));
}

// 监督者状态寄存器, sstatus

#define SSTATUS_SPP (1L << 8)  // 先前模式, 1=监督者, 0=用户
#define SSTATUS_SPIE (1L << 5) // 监督者先前中断使能
#define SSTATUS_UPIE (1L << 4) // 用户先前中断使能
#define SSTATUS_SIE (1L << 1)  // 监督者中断使能
#define SSTATUS_UIE (1L << 0)  // 用户中断使能

static inline uint64
r_sstatus()
{
  uint64 x;
  asm volatile("csrr %0, sstatus" : "=r" (x) );
  return x;
}

static inline void 
w_sstatus(uint64 x)
{
  asm volatile("csrw sstatus, %0" : : "r" (x));
}

// 监督者中断挂起寄存器
static inline uint64
r_sip()
{
  uint64 x;
  asm volatile("csrr %0, sip" : "=r" (x) );
  return x;
}

static inline void 
w_sip(uint64 x)
{
  asm volatile("csrw sip, %0" : : "r" (x));
}

// 监督者中断使能寄存器
#define SIE_SEIE (1L << 9) // 外部中断
#define SIE_STIE (1L << 5) // 时钟中断
#define SIE_SSIE (1L << 1) // 软件中断
static inline uint64
r_sie()
{
  uint64 x;
  asm volatile("csrr %0, sie" : "=r" (x) );
  return x;
}

static inline void 
w_sie(uint64 x)
{
  asm volatile("csrw sie, %0" : : "r" (x));
}

// 机器模式中断使能寄存器
#define MIE_STIE (1L << 5)  // 监督者时钟中断
static inline uint64
r_mie()
{
  uint64 x;
  asm volatile("csrr %0, mie" : "=r" (x) );
  return x;
}

static inline void 
w_mie(uint64 x)
{
  asm volatile("csrw mie, %0" : : "r" (x));
}

// 监督者异常程序计数器，保存异常返回后
// 将要执行的指令地址。
static inline void
w_sepc(uint64 x)
{
  asm volatile("csrw sepc, %0" : : "r" (x));
}

static inline uint64
r_sepc()
{
  uint64 x;
  asm volatile("csrr %0, sepc" : "=r" (x) );
  return x;
}

// 机器异常委托寄存器
static inline uint64
r_medeleg()
{
  uint64 x;
  asm volatile("csrr %0, medeleg" : "=r" (x) );
  return x;
}

static inline void 
w_medeleg(uint64 x)
{
  asm volatile("csrw medeleg, %0" : : "r" (x));
}

// 机器中断委托寄存器
static inline uint64
r_mideleg()
{
  uint64 x;
  asm volatile("csrr %0, mideleg" : "=r" (x) );
  return x;
}

static inline void 
w_mideleg(uint64 x)
{
  asm volatile("csrw mideleg, %0" : : "r" (x));
}

// 监督者陷阱向量基地址
// 低两位是模式
static inline void
w_stvec(uint64 x)
{
  asm volatile("csrw stvec, %0" : : "r" (x));
}

static inline uint64
r_stvec()
{
  uint64 x;
  asm volatile("csrr %0, stvec" : "=r" (x) );
  return x;
}

// 监督者时钟比较寄存器
static inline uint64
r_stimecmp()
{
  uint64 x;
  // asm volatile("csrr %0, stimecmp" : "=r" (x) );
  asm volatile("csrr %0, 0x14d" : "=r" (x) );
  return x;
}

static inline void 
w_stimecmp(uint64 x)
{
  // asm volatile("csrw stimecmp, %0" : : "r" (x));
  asm volatile("csrw 0x14d, %0" : : "r" (x));
}

// 机器环境配置寄存器
static inline uint64
r_menvcfg()
{
  uint64 x;
  // asm volatile("csrr %0, menvcfg" : "=r" (x) );
  asm volatile("csrr %0, 0x30a" : "=r" (x) );
  return x;
}

static inline void 
w_menvcfg(uint64 x)
{
  // asm volatile("csrw menvcfg, %0" : : "r" (x));
  asm volatile("csrw 0x30a, %0" : : "r" (x));
}

// 物理内存保护
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

// 使用 RISC-V 的 Sv39 分页方案。
#define SATP_SV39 (8L << 60)

#define MAKE_SATP(pagetable) (SATP_SV39 | (((uint64)pagetable) >> 12))

// 监督者地址转换和保护寄存器；
// 保存页表的地址。
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

// 监督者陷阱原因寄存器
static inline uint64
r_scause()
{
  uint64 x;
  asm volatile("csrr %0, scause" : "=r" (x) );
  return x;
}

// 监督者陷阱值寄存器
static inline uint64
r_stval()
{
  uint64 x;
  asm volatile("csrr %0, stval" : "=r" (x) );
  return x;
}

// 机器模式计数器使能寄存器
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

// 机器模式周期计数器
static inline uint64
r_time()
{
  uint64 x;
  asm volatile("csrr %0, time" : "=r" (x) );
  return x;
}

// 使能设备中断
static inline void
intr_on()
{
  w_sstatus(r_sstatus() | SSTATUS_SIE);
}

// 禁能设备中断
static inline void
intr_off()
{
  w_sstatus(r_sstatus() & ~SSTATUS_SIE);
}

// 设备中断是否已使能？
static inline int
intr_get()
{
  uint64 x = r_sstatus();
  return (x & SSTATUS_SIE) != 0;
}

static inline uint64
r_sp()
{
  uint64 x;
  asm volatile("mv %0, sp" : "=r" (x) );
  return x;
}

// 读写 tp (线程指针)，xv6 用它来保存
// 当前核心的 hartid (核心编号)，即 cpus[] 数组的索引。
static inline uint64
r_tp()
{
  uint64 x;
  asm volatile("mv %0, tp" : "=r" (x) );
  return x;
}

static inline void 
w_tp(uint64 x)
{
  asm volatile("mv tp, %0" : : "r" (x));
}

static inline uint64
r_ra()
{
  uint64 x;
  asm volatile("mv %0, ra" : "=r" (x) );
  return x;
}

// 刷新 TLB
static inline void
sfence_vma()
{
  // 两个 zero 表示刷新所有 TLB 条目。
  asm volatile("sfence.vma zero, zero");
}

typedef uint64 pte_t;       // 页表项
typedef uint64 *pagetable_t; // 512 个 PTE

#endif // __ASSEMBLER__

#define PGSIZE 4096 // 每页字节数
#define PGSHIFT 12  // 页内偏移的位数

#define PGROUNDUP(sz)  (((sz)+PGSIZE-1) & ~(PGSIZE-1)) // 向上取整到页边界
#define PGROUNDDOWN(a) (((a)) & ~(PGSIZE-1)) // 向下取整到页边界

#define PTE_V (1L << 0) // 有效
#define PTE_R (1L << 1) // 可读
#define PTE_W (1L << 2) // 可写
#define PTE_X (1L << 3) // 可执行
#define PTE_U (1L << 4) // 用户可访问

// 将物理地址转换为 PTE 格式
#define PA2PTE(pa) ((((uint64)pa) >> 12) << 10)

// 将 PTE 转换为物理地址
#define PTE2PA(pte) (((pte) >> 10) << 12)

// 获取 PTE 的标志位
#define PTE_FLAGS(pte) ((pte) & 0x3FF)

// 从虚拟地址中提取三级 9 位页表索引。
#define PXMASK          0x1FF // 9 位掩码
#define PXSHIFT(level)  (PGSHIFT+(9*(level)))
#define PX(level, va) ((((uint64) (va)) >> PXSHIFT(level)) & PXMASK)

// 超过最高可能虚拟地址的值。
// MAXVA 实际上比 Sv39 允许的最大值小一位，
// 以避免对设置了最高位的虚拟地址进行符号扩展。
#define MAXVA (1L << (9 + 9 + 9 + 12 - 1))
