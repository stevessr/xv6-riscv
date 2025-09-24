#include "kernel/types.h"
#include "user/user.h"

int main()
{
    unsigned int i = 0x00646c72;
    printf("i=%d\n", i);
    printf("i=%x\n", i);
    printf("57616=%x\n", 57616);
    printf("H%x Wo%s\n", 57616, (char *)&i);
    //printf("x=%d y=%d\n", 3);
    return 0;
}