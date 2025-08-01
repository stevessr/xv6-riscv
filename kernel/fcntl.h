// 文件控制选项 (file control options)
// 这些宏定义了在打开文件时可以使用的不同标志。
// open() 系统调用使用这些标志来确定文件的访问模式和行为。

#define O_RDONLY  0x000  // 以只读方式打开文件。
#define O_WRONLY  0x001  // 以只写方式打开文件。
#define O_RDWR    0x002  // 以读写方式打开文件。
#define O_CREATE  0x200  // 如果文件不存在，则创建新文件。
#define O_TRUNC   0x400  // 如果文件存在，并且以可写的方式打开，则将其长度截断为零。
