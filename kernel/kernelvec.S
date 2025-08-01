        #
        # 在 supervisor 模式下的中断和异常会来到这里。
        #
        #
        # 当前的栈是内核栈。
        # 推入寄存器，调用 kerneltrap()。
        # 当 kerneltrap() 返回时，恢复寄存器，然后返回。
        #

.globl kerneltrap # .globl 伪指令使得符号 kerneltrap 对于链接器是可见的，因此可以从其他文件引用。
.globl kernelvec  # 同上，使得 kernelvec 可见
.align 4          # .align 4 确保接下来的代码对齐在4字节边界上，这可以提高 RISC-V 上的性能。

# kernelvec 是内核陷阱向量的标签。
# 当在 supervisor 模式下发生陷阱（异常、中断或系统调用）时，
# CPU 会跳转到存储在 stvec（supervisor trap vector）寄存器中的地址，该地址指向 kernelvec。
kernelvec:
        # 在当前进程的内核栈上分配256字节。
        # 这块空间用于保存寄存器。
        # RISC-V 调用约定将寄存器分为调用者保存和被调用者保存的寄存器。
        # 陷阱可能在任何时候发生，所以我们必须保存所有可能被中断代码使用的寄存器。
        addi sp, sp, -256

        # 将通用寄存器保存到栈上。
        # sd 代表 "store doubleword" (64位)。
        # 这里保存了 ra, sp, gp, tp, t0-t2, a0-a7, 和 t3-t6 寄存器。
        # 注意：并非所有寄存器都被保存。例如，被调用者保存的寄存器（s0-s11）在这里没有保存。
        # 这是因为 C 代码（kerneltrap）会按照调用约定保存它所使用的任何被调用者保存的寄存器。
        sd ra, 0(sp)
        sd sp, 8(sp)
        sd gp, 16(sp)
        sd tp, 24(sp)
        sd t0, 32(sp)
        sd t1, 40(sp)
        sd t2, 48(sp)
        sd a0, 72(sp)
        sd a1, 80(sp)
        sd a2, 88(sp)
        sd a3, 96(sp)
        sd a4, 104(sp)
        sd a5, 112(sp)
        sd a6, 120(sp)
        sd a7, 128(sp)
        sd t3, 216(sp)
        sd t4, 224(sp)
        sd t5, 232(sp)
        sd t6, 240(sp)

        # 调用位于 trap.c 中的 C 函数 kerneltrap。
        # 这个函数将处理陷阱，确定其原因，并采取适当的行动。
        # kerneltrap 从控制寄存器（如 scause, sepc, stval）中读取陷阱的原因。
        call kerneltrap

        # 在 kerneltrap 返回后，从栈中恢复之前保存的寄存器。
        # ld 代表 "load doubleword"。
        ld ra, 0(sp)
        ld sp, 8(sp)
        ld gp, 16(sp)
        # 不恢复 tp 寄存器（其中包含 hartid），因为进程可能已经被调度到另一个CPU上运行。
        # tp（线程指针，寄存器 x4）用于存储当前 CPU 的 ID (hartid)。
        # 如果恢复旧的 tp，其值可能是不正确的。
        ld t0, 32(sp)
        ld t1, 40(sp)
        ld t2, 48(sp)
        ld a0, 72(sp)
        ld a1, 80(sp)
        ld a2, 88(sp)
        ld a3, 96(sp)
        ld a4, 104(sp)
        ld a5, 112(sp)
        ld a6, 120(sp)
        ld a7, 128(sp)
        ld t3, 216(sp)
        ld t4, 224(sp)
        ld t5, 232(sp)
        ld t6, 240(sp)

        # 释放栈上的256字节，将栈指针恢复到陷阱发生前的位置。
        addi sp, sp, 256

        # sret "supervisor return" 指令，用于从陷阱返回。
        # 它会：
        # 1. 将程序计数器（pc）设置为 sepc（supervisor exception program counter）寄存器中的值，
        #    sepc 中保存着导致陷阱的指令地址。
        # 2. 将特权级别从 supervisor 模式改回陷阱发生前的模式。
        # 3. 通过更新 sstatus 寄存器重新启用中断。
        sret
