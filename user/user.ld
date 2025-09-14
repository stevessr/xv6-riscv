OUTPUT_ARCH( "riscv" ) /* 指定输出目标体系结构为riscv */

SECTIONS
{
 . = 0x0; /* 用户程序的起始地址为0 */
 
  .text : { /* 代码段 */
    *(.text .text.*)
  }

  .rodata : { /* 只读数据段 */
    . = ALIGN(16);
    *(.srodata .srodata.*)
    . = ALIGN(16);
    *(.rodata .rodata.*)
  }

  .eh_frame : { /* 异常处理帧 */
       *(.eh_frame)
       *(.eh_frame.*)
   }

  . = ALIGN(0x1000); /* 对齐到4K边界 */
  .data : { /* 已初始化数据段 */
    . = ALIGN(16);
    *(.sdata .sdata.*)
    . = ALIGN(16);
    *(.data .data.*)
  }

  .bss : { /* 未初始化数据段 */
    . = ALIGN(16);
    *(.sbss .sbss.*)
    . = ALIGN(16);
    *(.bss .bss.*)
  }

  PROVIDE(end = .); /* 定义end符号，表示所有段的结束位置 */
}