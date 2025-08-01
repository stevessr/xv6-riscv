/*
 * xv6-riscv 内核链接器脚本
 *
 * 这个文件指导链接器(ld)如何将编译后的各个目标文件(.o)组合成一个单一的内核可执行文件。
 * 它定义了内核在内存中的布局，包括各个段（如代码、只读数据、数据段）的加载地址和顺序。
 */

/*
 * OUTPUT_ARCH("riscv")
 * 指定输出的可执行文件的目标架构为 RISC-V。
 * 这确保链接器生成适用于 RISC-V 平台的代码。
 */
OUTPUT_ARCH( "riscv" )

/*
 * ENTRY(_entry)
 * 设置程序的入口点为 _entry 标签。
 * 当内核被加载到内存后，CPU将从 _entry 标签所在的位置开始执行第一条指令。
 * _entry 标签在 kernel/entry.S 文件中定义。
 */
ENTRY( _entry )

/*
 * SECTIONS
 * 定义了输出文件的内存布局。
 * 链接器将根据这里的规则，把输入目标文件中的各个 section 放入输出文件的指定位置。
 */
SECTIONS
{
  /*
   * 设置程序的起始地址为 0x80000000。
   * 这个地址是 QEMU 'virt' 机器加载内核的物理内存地址 (DRAM base)。
   * '.' 是一个特殊的链接器变量，代表当前位置计数器。
   * 这行代码确保 _entry (即程序的入口点) 被放置在 0x80000000。
   */
  . = 0x80000000;

  /*
   * .text 段 (代码段)
   * 存放程序的可执行指令。
   */
  .text : {
    *(.text .text.*)    /* 包含所有输入文件的 .text 和 .text.* section */
    . = ALIGN(0x1000);  /* 将当前位置对齐到 4K (一个页) 的边界 */
    _trampoline = .;    /* 定义 _trampoline 符号，指向 trampoline 代码的起始位置 */
    *(trampsec)         /* 包含 trampoline.S 中定义的 trampsec section */
    . = ALIGN(0x1000);  /* 再次对齐到 4K 页边界 */
    /* 检查 trampoline 代码的大小是否恰好为一个页 (4096 字节) */
    ASSERT(. - _trampoline == 0x1000, "error: trampoline larger than one page");
    PROVIDE(etext = .); /* 定义 etext 符号，标记代码段的结束位置 */
  }

  /*
   * .rodata 段 (只读数据段)
   * 存放常量数据，如字符串字面量。
   */
  .rodata : {
    . = ALIGN(16);      /* 16字节对齐，优化访问速度 */
    *(.srodata .srodata.*) /* small read-only data, 与 .rodata 合并处理 */
    . = ALIGN(16);
    *(.rodata .rodata.*) /* 包含所有输入文件的 .rodata 和 .rodata.* section */
  }

  /*
   * .data 段 (已初始化数据段)
   * 存放已初始化的全局变量和静态变量。
   */
  .data : {
    . = ALIGN(16);
    *(.sdata .sdata.*)   /* small data, 与 .data 合并处理 */
    . = ALIGN(16);
    *(.data .data.*)     /* 包含所有输入文件的 .data 和 .data.* section */
  }

  /*
   * .bss 段 (未初始化数据段)
   * 存放未初始化的全局变量和静态变量。
   * 内核启动时会负责将这块内存区域清零。
   */
  .bss : {
    . = ALIGN(16);
    *(.sbss .sbss.*)     /* small bss, 与 .bss 合并处理 */
    . = ALIGN(16);
    *(.bss .bss.*)       /* 包含所有输入文件的 .bss 和 .bss.* section */
  }

  /*
   * 定义 end 符号，标记内核所有段 (代码、只读数据、数据、BSS) 的结束位置。
   * 从 end 开始到 PHYSTOP (在 memlayout.h 中定义) 的物理内存将由页分配器管理。
   */
  PROVIDE(end = .);
}
