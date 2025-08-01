// ELF 可执行文件的格式

#define ELF_MAGIC 0x464C457FU  // 小端模式下的 "\x7FELF"

// 文件头
struct elfhdr {
  uint magic;      // 必须等于 ELF_MAGIC
  uchar elf[12];
  ushort type;      // 1=可重定位, 2=可执行, 3=共享目标, 4=核心镜像
  ushort machine;   // 必须是 EM_RISCV
  uint version;    // 必须是 1
  uint64 entry;     // 程序入口点的虚拟地址
  uint64 phoff;     // 程序头表的文件偏移量
  uint64 shoff;     // 节头表的文件偏移量
  uint flags;
  ushort ehsize;    // 此 ELF 头的大小
  ushort phentsize; // 程序头条目的大小
  ushort phnum;     // 程序头条目的数量
  ushort shentsize; // 节头条目的大小
  ushort shnum;     // 节头条目的数量
  ushort shstrndx;  // 节名字符串表的节头索引
};

// 程序节头
struct proghdr {
  uint32 type;   // 段的类型
  uint32 flags;  // 段标志
  uint64 off;    // 段的文件偏移量
  uint64 vaddr;  // 段的虚拟地址
  uint64 paddr;  // 段的物理地址
  uint64 filesz; // 段在文件中的大小
  uint64 memsz;  // 段在内存中的大小
  uint64 align;  // 段对齐
};

// proghdr 类型的宏定义
#define ELF_PROG_LOAD           1 // 可加载段

// proghdr 标志的位掩码
#define ELF_PROG_FLAG_EXEC      1 // 可执行
#define ELF_PROG_FLAG_WRITE     2 // 可写
#define ELF_PROG_FLAG_READ      4 // 可读
