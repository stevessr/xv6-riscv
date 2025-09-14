#
        # 处理从用户空间到内核的陷阱的底层代码，
        # 以及从内核返回到用户空间。
        #
        # 内核将持有此代码的页面映射到
        # 用户和内核空间中相同的虚拟地址（TRAMPOLINE），
        # 以便在切换页表时它能继续工作。
        # kernel.ld使此代码从页面边界开始。
        #

#include "riscv.h"
#include "memlayout.h"

.section trampsec
.globl trampoline
.globl usertrap
trampoline:
.align 4
.globl uservec
uservec:    
	#
        # trap.c 将 stvec 指向这里，所以
        # 来自用户空间的陷阱从这里开始，
        # 在 supervisor 模式下，但使用
        # 用户页表。
        #

        # 将用户 a0 保存在 sscratch 中，以便
        # a0 可以用来访问 TRAPFRAME。
        csrw sscratch, a0

        # 每个进程都有一个单独的 p->trapframe 内存区域，
        # 但它在每个进程的用户页表中都映射到相同的虚拟地址
        # (TRAPFRAME)。
        li a0, TRAPFRAME
        
        # 将用户寄存器保存在 TRAPFRAME 中
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

	# 将用户 a0 保存在 p->trapframe->a0 中
        csrr t0, sscratch
        sd t0, 112(a0)

        # 从 p->trapframe->kernel_sp 初始化内核栈指针
        ld sp, 8(a0)

        # 使 tp 持有当前的 hartid，来自 p->trapframe->kernel_hartid
        ld tp, 32(a0)

        # 从 p->trapframe->kernel_trap 加载 usertrap() 的地址
        ld t0, 16(a0)

        # 从 p->trapframe->kernel_satp 获取内核页表地址
        ld t1, 0(a0)

        # 等待任何先前的内存操作完成，以便
        # 它们使用用户页表。
        sfence.vma zero, zero

        # 安装内核页表。
        csrw satp, t1

        # 从TLB中刷新现在已经过时的用户条目。
        sfence.vma zero, zero

        # 调用 usertrap()
        jalr t0

.globl userret
userret:
        # usertrap() 在这里返回，用户 satp 在 a0 中。
        # 从内核返回到用户。

        # 切换到用户页表。
        sfence.vma zero, zero
        csrw satp, a0
        sfence.vma zero, zero

        li a0, TRAPFRAME

        # 从 TRAPFRAME 恢复除 a0 之外的所有寄存器
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

	# 恢复用户 a0
        ld a0, 112(a0)
        
        # 返回到用户模式和用户pc。
        # usertrapret() 设置了 sstatus 和 sepc。
        sret