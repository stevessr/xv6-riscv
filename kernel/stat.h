#include "types.h"

// 文件类型定义
#define T_DIR     1   // 目录 (Directory)
#define T_FILE    2   // 普通文件 (File)
#define T_DEVICE  3   // 设备文件 (Device)

// stat 结构体，用于描述一个文件的元数据信息
// 这个结构体的信息可以通过 stat 或 fstat 系统调用获取
struct stat {
  int dev;     // 文件所在的设备号 (File system's disk device)
  uint ino;    // Inode 编号 (Inode number)，在设备内是唯一的
  short type;  // 文件类型 (Type of file)，例如 T_DIR, T_FILE, T_DEVICE
  short nlink; // 硬链接数量 (Number of links to file)，当 nlink 为 0 时，文件才会被删除
  uint64 size; // 文件大小，单位为字节 (Size of file in bytes)
};
