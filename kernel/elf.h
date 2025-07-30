// ELF 可执行文件格式

#define ELF_MAGIC 0x464C457FU  // 小端字节序的 "\x7FELF"

// 文件头
struct elfhdr {
  uint magic;      // 必须等于 ELF_MAGIC
  uchar elf[12];
  ushort type;     // 1=relocatable, 2=executable, 3=shared object, 4=core image
  ushort machine;  // 必须是 EM_RISCV
  uint version;    // 必须是 1
  uint64 entry;    // 程序入口点的虚拟地址
  uint64 phoff;    // 程序头表的文件偏移
  uint64 shoff;    // 节头表的文件偏移
  uint flags;
  ushort ehsize;   // ELF 头的大小
  ushort phentsize;// 程序头表项的大小
  ushort phnum;    // 程序头表项的数量
  ushort shentsize;// 节头表项的大小
  ushort shnum;    // 节头表项的数量
  ushort shstrndx; // 节名字符串表的索引
};

// 程序段头
struct proghdr {
  uint32 type;   // 段类型
  uint32 flags;  // 段标志
  uint64 off;    // 段在文件中的偏移
  uint64 vaddr;  // 段的虚拟地址
  uint64 paddr;  // 段的物理地址
  uint64 filesz; // 段在文件中的大小
  uint64 memsz;  // 段在内存中的大小
  uint64 align;  // 段对齐要求
};

// proghdr.type 的值
#define ELF_PROG_LOAD           1 // 可加载段

// proghdr.flags 的标志位
#define ELF_PROG_FLAG_EXEC      1 // 可执行
#define ELF_PROG_FLAG_WRITE     2 // 可写
#define ELF_PROG_FLAG_READ      4 // 可读
