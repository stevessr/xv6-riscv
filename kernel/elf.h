// Format of an ELF executable file
// ELF可执行文件的格式

#define ELF_MAGIC 0x464C457FU // "\x7FELF" in little endian
                              // 小端格式的 "\x7FELF"

// File header
// 文件头
struct elfhdr
{
  uint magic; // must equal ELF_MAGIC 必须等于ELF_MAGIC
  uchar elf[12];
  ushort type;      // 1=relocatable, 2=executable, 3=shared, 4=core
  ushort machine;   // must be 243 (RISC-V)
  uint version;     // must be 1
  uint64 entry;     // 程序的入口点虚拟地址
  uint64 phoff;     // 程序头表的偏移量
  uint64 shoff;     // 节头表的偏移量
  uint flags;       // 处理器特定标志
  ushort ehsize;    // ELF头的大小
  ushort phentsize; // 程序头表项的大小
  ushort phnum;     // 程序头表项的数量
  ushort shentsize; // 节头表项的大小
  ushort shnum;     // 节头表项的数量
  ushort shstrndx;  // 节头字符串表索引
};

// Program section header
// 程序节头
struct proghdr
{
  uint32 type;   // 段类型
  uint32 flags;  // 段标志
  uint64 off;    // 段在文件中的偏移量
  uint64 vaddr;  // 段的虚拟地址
  uint64 paddr;  // 段的物理地址
  uint64 filesz; // 段在文件中的大小
  uint64 memsz;  // 段在内存中的大小
  uint64 align;  // 段的对齐方式
};

// Values for Proghdr type
// Proghdr类型的取值
#define ELF_PROG_LOAD 1 // 可加载段

// Flag bits for Proghdr flags
// Proghdr标志位的取值
#define ELF_PROG_FLAG_EXEC 1  // 可执行
#define ELF_PROG_FLAG_WRITE 2 // 可写
#define ELF_PROG_FLAG_READ 4  // 可读