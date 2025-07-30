        # qemu -kernel 将内核加载到 0x80000000
        # 并使每个 hart (即 CPU) 跳转到那里。
        # kernel.ld 使得以下代码被放置在 0x80000000。
.section .text
.global _entry
_entry:
        # 为 C 代码设置一个栈。
        # stack0 在 start.c 中声明，
        # 每个 CPU 有一个 4096 字节的栈。
        # sp = stack0 + (hartid * 4096)
        la sp, stack0       # 加载 stack0 的地址到 sp (栈指针)
        li a0, 1024*4     # 将 4096 (栈大小) 加载到 a0
        csrr a1, mhartid  # 读取 hartid (硬件线程ID) 到 a1
        addi a1, a1, 1      # hartid 从 0 开始，所以加 1
        mul a0, a0, a1      # 计算偏移量 (hartid * 栈大小)
        add sp, sp, a0      # 设置当前 hart 的栈顶
        # 跳转到 start.c 中的 start() 函数
        call start
spin:
        j spin              # 无限循环，以防 start() 返回
