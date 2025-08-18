#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
void send(int, int, char, char, char *);

int main(int argc, char *argv[])
{
    int p1[2], p2[2], p3[2];
    if (pipe(p1) == 0 && pipe(p2) == 0 && pipe(p3) == 0)
    {
        int pid = fork();
        if (pid == 0)
        {
            pid = fork();
            if (pid == 0)
            {
                send(p1[1], p2[0], '1', '0', "ping");
            }
            else
            {
                send(p2[1], p3[0], '2', '1', "pong");
            }
        }
        else
        {
            send(p3[1], p1[0], '0', '2', "ding");
        }
    }
    else
    {
        printf("error in creating");
    }
    return 0;
}

void send(int in, int out, char data, char verify, char *str)
{
    char buf[64];
    write(out, &data, sizeof(data));
    close(out);
    if (read(in, buf, sizeof(buf)) > 0 && buf[0] == verify)
    {
        printf("%d get %s\n", getpid(), str);
    }
    else
    {
        printf("%d get error responce %s\n", getpid(), str);
    }
    close(in);
}