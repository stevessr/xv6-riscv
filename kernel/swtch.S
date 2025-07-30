# Context switch (上下文切换)
#
#   void swtch(struct context *old, struct context *new);
#   a0: 旧上下文的指针 (struct context *old)
#   a1: 新上下文的指针 (struct context *new)
# 
# 将当前寄存器保存在 old 指向的结构体中。从 new 指向的结构体中加载寄存器。

.globl swtch
swtch:
        # 保存旧上下文的寄存器
        # 根据 RISC-V 调用约定，ra (返回地址) 和 s0-s11 (被调用者保存的寄存器)
        # 必须由被调用的函数（即本函数 swtch）保存。
        # sp (栈指针) 也需要保存。
        # 将这些寄存器的值存入 old 结构体中 (a0 指向 old)。
        sd ra, 0(a0)
        sd sp, 8(a0)
        sd s0, 16(a0)
        sd s1, 24(a0)
        sd s2, 32(a0)
        sd s3, 40(a0)
        sd s4, 48(a0)
        sd s5, 56(a0)
        sd s6, 64(a0)
        sd s7, 72(a0)
        sd s8, 80(a0)
        sd s9, 88(a0)
        sd s10, 96(a0)
        sd s11, 104(a0)

        # 加载新上下文的寄存器
        # 从 new 结构体中 (a1 指向 new) 加载值到寄存器。
        ld ra, 0(a1)
        ld sp, 8(a1)
        ld s0, 16(a1)
        ld s1, 24(a1)
        ld s2, 32(a1)
        ld s3, 40(a1)
        ld s4, 48(a1)
        ld s5, 56(a1)
        ld s6, 64(a1)
        ld s7, 72(a1)
        ld s8, 80(a1)
        ld s9, 88(a1)
        ld s10, 96(a1)
        ld s11, 104(a1)
        
        # 返回到新加载的返回地址 (ra)。
        # 这会使程序在新上下文的栈上继续执行，
        # 从而完成了上下文切换。
        ret

	
