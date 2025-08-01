typedef unsigned int   uint;   // 通用无符号整数，常用于计数、大小和索引。
typedef unsigned short ushort; // 无符号短整数，在需要节省空间时使用。
typedef unsigned char  uchar;  // 无符号字符，用于处理字节数据或ASCII字符。

// 固定宽度的整数类型，确保在不同平台上有一致的大小。
// 这对于处理硬件寄存器、文件系统和网络协议等底层数据结构至关重要。
typedef unsigned char uint8;   // 8位无符号整数。
typedef unsigned short uint16; // 16位无符号整数。
typedef unsigned int  uint32;  // 32位无符号整数。
typedef unsigned long uint64;  // 64位无符号整数，也用于表示物理地址。

// RISC-V 64位架构下页表项（Page Table Entry）的类型。
// xv6 使用它来存储物理页号以及相关的权限和状态标志位。
typedef uint64 pde_t;
