# execs /init 的初始进程。
# 这段代码在用户空间运行。

#include "syscall.h"

# exec(init, argv)
.globl start
start:
        la a0, init     # a0 = &init
        la a1, argv     # a1 = &argv
        li a7, SYS_exec # a7 = exec 系统调用号
        ecall           # 发起系统调用

# for(;;) exit();
exit:
        li a7, SYS_exit # a7 = exit 系统调用号
        ecall           # 发起系统调用
        jal exit        # 无限循环，以防 exit 失败

# char init[] = "/init\0";
init:
  .string "/init\0"

# char *argv[] = { init, 0 };
.p2align 2
argv:
  .quad init
  .quad 0
