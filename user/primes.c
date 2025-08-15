#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

// 函数原型
void sieve(int left_pipe_read_end);

int main(int argc, char *argv[])
{
    // 创建第一个管道
    int p[2];
    if (pipe(p) < 0) {
        fprintf(2, "pipe failed\n");
        exit(1);
    }

    int pid = fork();

    if (pid < 0) {
        fprintf(2, "fork failed\n");
        exit(1);
    }

    if (pid == 0) {
        // 子进程：启动筛选过程
        // 关闭第一个管道的写端，因为它只需要从中读取
        close(p[1]);
        sieve(p[0]); // 将读端传递给筛选函数
    } else {
        // 父进程：初始数据生产者
        // 关闭第一个管道的读端，因为它只需要向其中写入
        close(p[0]);
        // 将数字 2 到 280 写入管道
        for (int i = 2; i <= 280; ++i) {
            if (write(p[1], &i, sizeof(i)) != sizeof(i)) {
                fprintf(2, "write error\n");
                exit(1);
            }
        }
        // 写完后关闭写端，这样下游进程才能读到EOF
        close(p[1]);
        // 等待子进程结束
        wait(0);
    }

    exit(0);
}

/**
 * @brief 递归的筛选函数
 * @param left_pipe_read_end 从左邻居进程接收数据的管道读端
 */
void sieve(int left_pipe_read_end) {
    int prime, num;
    
    // 1. 从左边管道读取第一个数，这个数必然是素数
    if (read(left_pipe_read_end, &prime, sizeof(prime)) <= 0) {
        // 如果管道为空或已关闭，说明没有更多数字了，结束进程
        close(left_pipe_read_end);
        exit(0);
    }

    // 2. 打印这个素数
    printf("prime %d\n", prime);

    // 3. 创建一个新的管道，用于连接到右邻居
    int right_pipe[2];
    if (pipe(right_pipe) < 0) {
        fprintf(2, "pipe failed\n");
        exit(1);
    }

    int pid = fork();

    if (pid < 0) {
        fprintf(2, "fork failed\n");
        exit(1);
    }

    if (pid == 0) {
        // 子进程（右邻居）
        // a. 关闭它不需要的管道端口
        close(right_pipe[1]); // 关闭右管道的写端
        close(left_pipe_read_end); // 不再需要从左管道读取

        // b. 递归调用sieve，处理下一阶段的筛选
        sieve(right_pipe[0]);
    } else {
        // 当前进程（作为父进程）
        // a. 关闭它不需要的管道端口
        close(right_pipe[0]); // 关闭右管道的读端

        // b. 从左管道持续读取数字
        while (read(left_pipe_read_end, &num, sizeof(num)) > 0) {
            // c. 如果数字不是当前素数的倍数，就通过右管道传给下一个进程
            if (num % prime != 0) {
                if (write(right_pipe[1], &num, sizeof(num)) != sizeof(num)) {
                    fprintf(2, "write error\n");
                    exit(1);
                }
            }
        }

        // d. 所有数字都处理完毕，关闭剩下的管道端口
        close(left_pipe_read_end);
        close(right_pipe[1]);

        // e. 等待自己的子进程结束，以确保整个链条正确终止
        wait(0);
    }
}