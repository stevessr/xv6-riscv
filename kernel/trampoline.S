        #
        # trampoline.S: 用户态/内核态切换的跳板
        #
        # 这段底层代码用于处理从用户空间到内核的陷入（trap），以及从内核返回用户空间。
        #
        # 内核将包含此代码的页面映射到用户和内核空间中相同的虚拟地址（TRAMPOLINE），
        # 这样在切换页表时，它仍然可以继续工作。
        #
        # kernel.ld 链接器脚本确保这段代码从一个页面边界开始。
        #

#include "riscv.h"
#include "memlayout.h"

# 声明一个名为 trampsec 的段，用于存放跳板代码
.section trampsec

# 全局符号，供其他文件引用
.globl trampoline
.globl usertrap
trampoline:
.align 4 # 4字节对齐
.globl uservec
uservec:    
	#
        # trap.c 中的 usertrapinit() 会将 stvec 寄存器设置为指向 uservec。
        # 因此，来自用户空间的所有陷入（中断、异常、系统调用）都会从这里开始执行。
        # 此时，CPU 处于监管者模式（supervisor mode），但页表仍然是用户的页表。
        #

        # 为了能够使用 a0 寄存器来访问 TRAPFRAME，
        # 首先需要将用户态的 a0 寄存器的值保存到 sscratch 寄存器中。
        # sscratch 是一个专为这类场景设计的暂存 CSR。
        csrw sscratch, a0

        # 加载 TRAPFRAME 的地址到 a0 寄存器。
        # 每个进程都有一个独立的 p->trapframe 内存区域，
        # 但在每个进程的用户页表中，它都被映射到相同的虚拟地址 TRAPFRAME。
        li a0, TRAPFRAME
        
        # 将所有用户寄存器保存到 a0 指向的 TRAPFRAME 中。
        # TRAPFRAME 的布局定义在 proc.h 中的 struct trapframe 中。
        # a0 已经被保存在 sscratch 中，所以这里先保存其他寄存器。
        sd ra, 40(a0)
        sd sp, 48(a0)
        sd gp, 56(a0)
        sd tp, 64(a0)
        sd t0, 72(a0)
        sd t1, 80(a0)
        sd t2, 88(a0)
        sd s0, 96(a0)
        sd s1, 104(a0)
        sd a1, 120(a0)
        sd a2, 128(a0)
        sd a3, 136(a0)
        sd a4, 144(a0)
        sd a5, 152(a0)
        sd a6, 160(a0)
        sd a7, 168(a0)
        sd s2, 176(a0)
        sd s3, 184(a0)
        sd s4, 192(a0)
        sd s5, 200(a0)
        sd s6, 208(a0)
        sd s7, 216(a0)
        sd s8, 224(a0)
        sd s9, 232(a0)
        sd s10, 240(a0)
        sd s11, 248(a0)
        sd t3, 256(a0)
        sd t4, 264(a0)
        sd t5, 272(a0)
        sd t6, 280(a0)

	# 从 sscratch 中读回用户态的 a0，并将其保存在 TRAPFRAME 中。
        csrr t0, sscratch
        sd t0, 112(a0)

        # 从 p->trapframe->kernel_sp 加载该进程的内核栈指针。
        ld sp, 8(a0)

        # 从 p->trapframe->kernel_hartid 加载当前 hart (cpu核心) 的 ID 到 tp 寄存器。
        # 在内核中，tp 寄存器约定为保存 hartid。
        ld tp, 32(a0)

        # 从 p->trapframe->kernel_trap 加载 C 语言的陷入处理函数 usertrap() 的地址。
        ld t0, 16(a0)

        # 从 p->trapframe->kernel_satp 获取内核页表的地址。
        ld t1, 0(a0)

        # 执行 sfence.vma 指令，确保所有之前的内存操作（它们使用的是用户页表）
        # 都已经完成。这是一个内存屏障。
        sfence.vma zero, zero

        # 将内核页表的地址写入 satp 寄存器，正式切换到内核地址空间。
        csrw satp, t1

        # 再次执行 sfence.vma，刷新 TLB（Translation Lookaside Buffer）。
        # 因为页表已经切换，旧的用户页表条目（PTE）在 TLB 中可能已经过时，需要使其失效。
        sfence.vma zero, zero

        # 跳转到 usertrap() 函数（地址在 t0 中），该函数不会返回到这里。
        # 从这里开始，执行 C 语言的内核代码。
        jr t0

.globl userret
userret:
        # userret(pagetable)
        # 由 trap.c 中的 usertrapret() 调用，用于从内核切换回用户态。
        # a0: 传入的用户页表的地址，用于设置 satp。

        # 在切换页表之前，刷新 TLB，确保所有内核内存访问都已完成。
        sfence.vma zero, zero

        # 将用户页表的地址写入 satp 寄存器，切换回用户地址空间。
        csrw satp, a0

        # 再次刷新 TLB，清除旧的内核页表条目。
        sfence.vma zero, zero

        # 将 TRAPFRAME 的地址加载到 a0，准备恢复寄存器。
        li a0, TRAPFRAME

        # 从 TRAPFRAME 中恢复除了 a0 之外的所有用户寄存器。
        ld ra, 40(a0)
        ld sp, 48(a0)
        ld gp, 56(a0)
        ld tp, 64(a0)
        ld t0, 72(a0)
        ld t1, 80(a0)
        ld t2, 88(a0)
        ld s0, 96(a0)
        ld s1, 104(a0)
        ld a1, 120(a0)
        ld a2, 128(a0)
        ld a3, 136(a0)
        ld a4, 144(a0)
        ld a5, 152(a0)
        ld a6, 160(a0)
        ld a7, 168(a0)
        ld s2, 176(a0)
        ld s3, 184(a0)
        ld s4, 192(a0)
        ld s5, 200(a0)
        ld s6, 208(a0)
        ld s7, 216(a0)
        ld s8, 224(a0)
        ld s9, 232(a0)
        ld s10, 240(a0)
        ld s11, 248(a0)
        ld t3, 256(a0)
        ld t4, 264(a0)
        ld t5, 272(a0)
        ld t6, 280(a0)

	# 最后恢复用户 a0 寄存器（通常是系统调用的返回值）。
        ld a0, 112(a0)
        
        # 执行 sret 指令，从监管者模式返回到用户模式。
        # usertrapret() 已经设置好了 sstatus (恢复之前的特权级和中断状态)
        # 和 sepc (设置返回到用户代码的地址)。
        sret
