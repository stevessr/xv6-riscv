#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/fs.h"

// find 函��在指定路径下递归地查找具有特定名���的文件
void find(char *path, const char *filename) {
    char path_buffer[512], *path_pointer;
    int file_descriptor;
    struct dirent directory_entry;
    struct stat status;

    // 打开路径对应的文件或目录
    if ((file_descriptor = open(path, 0)) < 0) {
        fprintf(2, "find: cannot open %s\n", path);
        return;
    }

    // 获��文件或目录的状态信息
    if (fstat(file_descriptor, &status) < 0) {
        fprintf(2, "find: cannot stat %s\n", path);
        close(file_descriptor);
        return;
    }

    // 根据文件类型进行处理
    switch (status.type) {
        // 如果是普通文件
        case T_FILE:
            // 从路径中提取文件名
            for (path_pointer = path + strlen(path); path_pointer >= path && *path_pointer != '/'; path_pointer--);
            path_pointer++;
            // 检查文件名是否与目标文件名匹配
            if (strcmp(path_pointer, filename) == 0) {
                printf("%s\n", path);
            }
            break;

        // 如果是目录
        case T_DIR:
            // 检查路径长度是否会��出缓冲区
            if (strlen(path) + 1 + DIRSIZ + 1 > sizeof path_buffer) {
                printf("find: path too long\n");
                break;
            }
            // 构建访问的���径
            strcpy(path_buffer, path);
            path_pointer = path_buffer + strlen(path_buffer);
            *path_pointer++ = '/';
            // 读取目录项
            while (read(file_descriptor, &directory_entry, sizeof(directory_entry)) == sizeof(directory_entry)) {
                // 跳过无效的目录项
                if (directory_entry.inum == 0)
                    continue;

                // 跳过 "." 和 ".." 目录
                if (strcmp(directory_entry.name, ".") == 0 || strcmp(directory_entry.name, "..") == 0)
                    continue;

                // 构建子文件或子目录的完整路径
                memmove(path_pointer, directory_entry.name, DIRSIZ);
                path_pointer[DIRSIZ] = 0;
                // 获取子文件或子目录的状态
                if (stat(path_buffer, &status) < 0) {
                    printf("find: cannot stat %s\n", path_buffer);
                    continue;
                }

                // 根据子项的类型进行处理
                switch (status.type) {
                    // 如果是���件，则检查文件名是否匹配
                    case T_FILE:
                        if (strcmp(directory_entry.name, filename) == 0) {
                            printf("%s\n", path_buffer);
                        }
                        break;
                    // 如果是目录，则递归调用 find
                    case T_DIR:
                        find(path_buffer, filename);
                        break;
                }
            }
            break;
    }
    // 关闭文件描述符
    close(file_descriptor);
}

// 主函数
int main(int argc, char *argv[]) {
    // 检查命��行参数的数量
    if (argc != 3) {
        fprintf(2, "Usage: find <directory> <filename>\n");
        exit(1);
    }

    // 调用 find 函数开始查找
    find(argv[1], argv[2]);

    exit(0);
}