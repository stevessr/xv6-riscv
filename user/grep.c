// 简单的 grep。只支持 ^, ., *, $ 操作符。

// 包含内核数据类型定义
#include "kernel/types.h"
// 包含文件状态信息
#include "kernel/stat.h"
// 包含文件控制选项
#include "kernel/fcntl.h"
// 包含用户态 API
#include "user/user.h"

char buf[1024];
int match(char*, char*);

// 在文件描述符 fd 中搜索 pattern
void
grep(char *pattern, int fd)
{
  int n, m;
  char *p, *q;

  m = 0;
  // 从 fd 读取数据到 buf
  while((n = read(fd, buf+m, sizeof(buf)-m-1)) > 0){
    m += n;
    buf[m] = '\0';
    p = buf;
    // 按行处理
    while((q = strchr(p, '\n')) != 0){
      *q = 0;
      // 如果当前行匹配 pattern
      if(match(pattern, p)){
        *q = '\n';
        // 打印匹配的行
        write(1, p, q+1 - p);
      }
      p = q+1;
    }
    // 将未处理完的数据移动到 buf 的开头
    if(m > 0){
      m -= p - buf;
      memmove(buf, p, m);
    }
  }
}

// 程序入口
int
main(int argc, char *argv[])
{
  int fd, i;
  char *pattern;

  // 参数检查
  if(argc <= 1){
    fprintf(2, "用法: grep pattern [file ...]\n");
    exit(1);
  }
  pattern = argv[1];

  // 如果没有指定文件，则从标准输入读取
  if(argc <= 2){
    grep(pattern, 0);
    exit(0);
  }

  // 遍历所有文件
  for(i = 2; i < argc; i++){
    if((fd = open(argv[i], O_RDONLY)) < 0){
      printf("grep: cannot open %s\n", argv[i]);
      exit(1);
    }
    grep(pattern, fd);
    close(fd);
  }
  exit(0);
}

// 正则表达式匹配器，来自 Kernighan & Pike,
// The Practice of Programming, Chapter 9, or
// https://www.cs.princeton.edu/courses/archive/spr09/cos333/beautiful.html

int matchhere(char*, char*);
int matchstar(int, char*, char*);

// 在 text 中搜索 re
int
match(char *re, char *text)
{
  // 如果 re 以 ^ 开头，则从 text 开头匹配
  if(re[0] == '^')
    return matchhere(re+1, text);
  // 否则，在 text 的任意位置进行匹配
  do{
    if(matchhere(re, text))
      return 1;
  }while(*text++ != '\0');
  return 0;
}

// matchhere: 在 text 的开头搜索 re
int matchhere(char *re, char *text)
{
  if(re[0] == '\0')
    return 1;
  if(re[1] == '*')
    return matchstar(re[0], re+2, text);
  if(re[0] == '$' && re[1] == '\0')
    return *text == '\0';
  if(*text!='\0' && (re[0]=='.' || re[0]==*text))
    return matchhere(re+1, text+1);
  return 0;
}

// matchstar: 在 text 的开头搜索 c*re
int matchstar(int c, char *re, char *text)
{
  // * 匹配零个或多个实例
  do{
    if(matchhere(re, text))
      return 1;
  }while(*text!='\0' && (*text++==c || c=='.'));
  return 0;
}
