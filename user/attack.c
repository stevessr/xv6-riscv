#include "kernel/types.h"
#include "kernel/fcntl.h"
#include "user/user.h"
#include "kernel/riscv.h"

int main(int argc, char *argv[])
{
  // 分配与 secret.c 相同的 32 页内存
  char *mem = sbrk(PGSIZE * 32);
  int total = PGSIZE * 32;
  char *allowed = "./abcdef";
  // 在分配的内存中搜索 8 字节字符串：7 可打印字符 + '\0'
  for(int off = 0; off <= total - 8; off++){
    char *p = mem + off;
    if(p[7] != '\0')
      continue;
    int ok = 1;
    for(int j = 0; j < 7; j++){
      char c = p[j];
      int found = 0;
      for(int k = 0; k < 8; k++){
        if(c == allowed[k]){ found = 1; break; }
      }
      if(!found){ ok = 0; break; }
    }
    if(ok){
      write(2, p, 8);
      exit(0);
    }
  }
  // 未找到
  exit(1);
}
