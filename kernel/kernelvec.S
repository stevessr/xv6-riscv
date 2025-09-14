# -*- coding: utf-8 -*-
        #
        # 在 supervisor 模式下的中断和异常会来到这里。
        #
        # 当前的栈是内核栈。
        # 推入寄存器，调用 kerneltrap()。
        # 当 kerneltrap() 返回时，恢复寄存器，然后返回。
        #
.globl kerneltrap
.globl kernelvec
.align 4
kernelvec:
        # 为保存寄存器腾出空间。
        addi sp, sp, -256

        # 保存调用者保存的寄存器。
        sd ra, 0(sp)
        # sd sp, 8(sp) # sp 不需要保存
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

        # 调用 trap.c 中的 C 语言陷阱处理程序
        call kerneltrap

        # 恢复寄存器。
        ld ra, 0(sp)
        # ld sp, 8(sp) # sp 不需要恢复
        ld gp, 16(sp)
        # 不恢复 tp (它包含 hartid)，以防我们切换了 CPU
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

        addi sp, sp, 256

        # 返回到我们在内核中所做的任何事情。
        sret