/* -*- coding: utf-8 -*- */
OUTPUT_ARCH( "riscv" ) /* 指定输出目标体系结构为riscv */
ENTRY( _entry ) /* 指定程序的入口点为_entry */

SECTIONS
{
  /*
   * 确保 entry.S / _entry 位于 0x80000000,
   * qemu的 -kernel 参数会跳转到这个地址。
   */
  . = 0x80000000; /* 将当前位置计数器设置为0x80000000 */

  .text : { /* 代码段 */
    kernel/entry.o(_entry) /* 将kernel/entry.o中的_entry放在最前面 */
    *(.text .text.*) /* 所有其他输入文件的.text和.text.*节 */
    . = ALIGN(0x1000); /* 对齐到4K边界 */
    _trampoline = .; /* 定义_trampoline符号为当前地址 */
    *(trampsec) /* 所有输入文件的trampsec节 */
    . = ALIGN(0x1000); /* 再次对齐到4K边界 */
    ASSERT(. - _trampoline == 0x1000, "error: trampoline larger than one page"); /* 断言trampoline的大小正好是一页 */
    PROVIDE(etext = .); /* 定义etext符号，表示代码段的结束 */
  }

  .rodata : { /* 只读数据段 */
    . = ALIGN(16); /* 16字节对齐 */
    *(.srodata .srodata.*) /* 小的只读数据 */
    . = ALIGN(16); /* 16字节对齐 */
    *(.rodata .rodata.*) /* 普通的只读数据 */
  }

  .data : { /* 已初始化数据段 */
    . = ALIGN(16); /* 16字节对齐 */
    *(.sdata .sdata.*) /* 小的已初始化数据 */
    . = ALIGN(16); /* 16字节对齐 */
    *(.data .data.*) /* 普通的已初始化数据 */
  }

  .bss : { /* 未初始化数据段 */
    . = ALIGN(16); /* 16字节对齐 */
    *(.sbss .sbss.*) /* 小的未初始化数据 */
    . = ALIGN(16); /* 16字节对齐 */
    *(.bss .bss.*) /* 普通的未初始化数据 */
  }

  PROVIDE(end = .); /* 定义end符号，表示所有段的结束位置 */
}