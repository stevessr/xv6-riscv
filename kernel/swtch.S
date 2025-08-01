#
# Context switch (上下文切换)
#
#   void swtch(struct context *old, struct context *new);
#
# 寄存器 a0: 指向旧上下文 (struct context *old) 的指针
# 寄存器 a1: 指向新上下文 (struct context *new) 的指针
#
# swtch 函数负责在两个内核线程之间切换。
# 它通过保存当前线程的寄存器（CPU状态）到 old 指向的 context 结构体，
# 然后从 new 指向的 context 结构体中加载新线程的寄存器来实现。
#

# .globl swtch 使得 swtch 标签对链接器可见，
# 这样其他C文件就可以调用这个汇编函数。
.globl swtch
swtch:
        # 保存旧线程的上下文
        # 根据 RISC-V 调用约定，ra (返回地址) 和 s0-s11 (被调用者保存的寄存器)
        # 是被调用函数（callee）需要负责保存的。
        # sp (栈指针) 也需要保存，因为它定义了当前线程的栈。
        # 这些寄存器共同构成了线程的执行上下文。
        #
        # struct context 定义在 kernel/proc.h 中，其字段顺序必须与这里的保存/加载顺序严格匹配。
        # a0 寄存器中存放的是 old context 的地址。
        # sd 指令 (store double-word) 将一个64位寄存器的值存入内存。
        sd ra, 0(a0)      # 保存返回地址 (return address)
        sd sp, 8(a0)      # 保存栈指针 (stack pointer)
        sd s0, 16(a0)     # 保存被调用者保存的寄存器 s0 (callee-saved register s0)
        sd s1, 24(a0)     # 保存 s1
        sd s2, 32(a0)     # 保存 s2
        sd s3, 40(a0)     # 保存 s3
        sd s4, 48(a0)     # 保存 s4
        sd s5, 56(a0)     # 保存 s5
        sd s6, 64(a0)     # 保存 s6
        sd s7, 72(a0)     # 保存 s7
        sd s8, 80(a0)     # 保存 s8
        sd s9, 88(a0)     # 保存 s9
        sd s10, 96(a0)    # 保存 s10
        sd s11, 104(a0)   # 保存 s11

        # 加载新线程的上下文
        # a1 寄存器中存放的是 new context 的地址。
        # ld 指令 (load double-word) 从内存中加载一个64位值到寄存器。
        ld ra, 0(a1)      # 加载新线程的返回地址
        ld sp, 8(a1)      # 加载新线程的栈指针
        ld s0, 16(a1)     # 加载新线程的 s0
        ld s1, 24(a1)     # 加载新线程的 s1
        ld s2, 32(a1)     # 加载新线程的 s2
        ld s3, 40(a1)     # 加载新线程的 s3
        ld s4, 48(a1)     # 加载新线程的 s4
        ld s5, 56(a1)     # 加载新线程的 s5
        ld s6, 64(a1)     # 加载新线程的 s6
        ld s7, 72(a1)     # 加载新线程的 s7
        ld s8, 80(a1)     # 加载新线程的 s8
        ld s9, 88(a1)     # 加载新线程的 s9
        ld s10, 96(a1)    # 加载新线程的 s10
        ld s11, 104(a1)   # 加载新线程的 s11

        # 返回到新加载的返回地址 (ra)
        # ret 指令实际上是 `jalr zero, 0(ra)` 的伪指令。
        # 它会跳转到 ra 寄存器中保存的地址。
        # 在这里，它会跳转到新线程之前被切换出去的地方（例如 scheduler 函数中的某个点），
        # 或者是一个新线程的启动函数（例如 forkret）。
        #
        # 一旦 ret 执行，CPU 将在新线程的栈上，使用新线程的寄存器状态开始执行。
        # 至此，上下文切换完成。
        ret
