# qemu -kernel 将内核加载到 0x80000000
# 并使每个 hart (即 CPU) 跳转到那里。
# kernel.ld 使得以下代码被放置在 0x80000000。
#
# 启动QEMU时，使用-kernel参数会将内核镜像加载到物理地址0x80000000处。
# 所有的CPU核心（hart）都会从这个地址开始执行指令。
# 链接器脚本 kernel.ld 确保了下面的代码段被放置在内存的起始位置 0x80000000。

.section .text
# .section .text 指令告诉汇编器，接下来的代码属于代码段（.text section）。

.global _entry
# .global _entry 声明了 _entry 标签为全局可见，这样链接器就能找到它作为程序的入口点。

_entry:
        # 为 C 代码设置一个栈。
        # stack0 在 start.c 中声明，
        # 每个 CPU 有一个 4096 字节的栈。
        # sp = stack0 + (hartid * 4096)
        #
        # 为即将执行的C语言代码设置栈空间。
        # stack0 是在 start.c 文件中定义的一个符号，指向一块内存区域的起始地址。
        # xv6为每个CPU核心都分配了一个4096字节（4KB）的栈空间。
        # 这块总的栈空间是连续的，我们需要根据当前CPU核心的ID（mhartid）计算出它自己的栈顶指针。
        # 栈指针 sp 的计算公式为：sp = stack0的基地址 + (hartid * 4096)

        la sp, stack0
        # la sp, stack0  (load address)
        # 这条指令将符号 stack0 的地址加载到栈指针寄存器 sp 中。
        # 执行后，sp 指向了所有CPU核心的总栈空间的起始位置。

        li a0, 1024*4
        # li a0, 1024*4 (load immediate)
        # 将立即数 4096 (4 * 1024) 加载到寄存器 a0。a0 在这里临时用作计算。

        csrr a1, mhartid
        # csrr a1, mhartid (control and status register read)
        # 读取控制状态寄存器 mhartid 的值到寄存器 a1。
        # mhartid 是一个特殊的寄存器，用于唯一标识一个硬件线程（即CPU核心）。

        addi a1, a1, 1
        # addi a1, a1, 1 (add immediate)
        # 将 a1 的值加 1。因为 hartid 从 0 开始，而栈的分配需要一个非零的乘数，
        # 所以这里将 hartid 映射为 1, 2, 3, ...

        mul a0, a0, a1
        # mul a0, a0, a1 (multiply)
        # 计算栈的偏移量。将 a0 (4096) 和 a1 (hartid + 1) 相乘，
        # 结果存回 a0。结果是 (hartid + 1) * 4096。

        add sp, sp, a0
        # add sp, sp, a0 (add)
        # 设置当前核心的栈顶指针。将基地址 sp (stack0) 与偏移量 a0 相加。
        # 这样，每个核心的栈空间就是 [stack0 + hartid*4096, stack0 + (hartid+1)*4096)。
        # RISC-V的栈是向下增长的，所以 sp 指向了栈的最高地址（栈顶）。

        # 跳转到 start.c 中的 start() 函数
        call start
        # call start
        # 调用 C 语言函数 start()，该函数在 start.c 中定义。
        # `call` 是一个伪指令，它会被汇编器展开为 `auipc ra, offset` 和 `jalr ra, offset(ra)`，
        # 用于跳转到 start 函数，并将返回地址保存在 ra 寄存器中。

spin:
        j spin
        # j spin (jump)
        # 这是一个无限循环。如果 start() 函数因为某些原因返回了（正常情况下不应该），
        # CPU 将会在此处无限循环，防止执行未知的内存区域。这是一种简单的保护机制。
