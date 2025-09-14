# qemu -kernel loads the kernel at 0x80000000
# qemu -kernel 将内核加载到 0x80000000
# and causes each hart (i.e. CPU) to jump there.
# 并使每个hart（即CPU）跳转到那里。
# kernel.ld causes the following code to
# kernel.ld 使以下代码
# be placed at 0x80000000.
# 放置在 0x80000000。
.section .text # 指定代码段
.global _entry # 声明全局符号_entry
_entry:
        # set up a stack for C.
        # 为C代码设置一个栈。
        # stack0 is declared in start.c,
        # stack0 在 start.c 中声明，
        # with a 4096-byte stack per CPU.
        # 每个CPU有4096字节的栈。
        # sp = stack0 + ((hartid + 1) * 4096)
        la sp, stack0 # 加载stack0的地址到sp寄存器
        li a0, 1024*4 # 加载4096到a0寄存器
        csrr a1, mhartid # 读取mhartid寄存器（hart id）到a1寄存器
        addi a1, a1, 1 # a1 = a1 + 1
        mul a0, a0, a1 # a0 = a0 * a1
        add sp, sp, a0 # sp = sp + a0，计算当前hart的栈顶地址
        # jump to start() in start.c
        # 跳转到 start.c 中的 start() 函数
        call start # 调用start函数
spin:
        j spin # 无限循环
