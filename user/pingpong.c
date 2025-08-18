#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

int main(int argc, char *argv[])
{
    int p2c[2]; // parent to child pipe
    int c2p[2]; // child to parent pipe
    char *byte = "1";
    char buf[1];

    // 创建两个管道：一个用于父进程向子进程发送，一个用于子进程向父进程发送
    if (pipe(p2c) < 0 || pipe(c2p) < 0)
    {
        printf("pipe failed\n");
        exit(1);
    }

    int pid = fork();
    if (pid < 0)
    {
        printf("fork failed\n");
        exit(1);
    }

    if (pid == 0)
    {
        close(p2c[1]);
        close(c2p[0]);

        // 从父进程读取数据
        if (read(p2c[0], buf, 1) == 1)
        {
            printf("%d: received ping\n", getpid());
        }

        // 向父进程发送数据
        write(c2p[1], &byte, 1);

        close(p2c[0]);
        close(c2p[1]);
        exit(0);
    }
    else
    {
        close(p2c[0]);
        close(c2p[1]);

        // 向子进程发送数据
        write(p2c[1], &byte, 1);

        // 等待子进程的响应
        if (read(c2p[0], buf, 1) == 1)
        {
            printf("%d: received pong\n", getpid());
        }

        close(p2c[1]);
        close(c2p[0]);
        wait(0); // 等待子进程结束
    }

    exit(0);
}