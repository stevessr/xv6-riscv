#include "kernel/types.h"
#include "kernel/stat.h"
#include "kernel/fcntl.h"
#include "user/user.h"

char buf[512];

// 计算文件或标准输入的行数、单词数和字符数
void
wc(int fd, char *name)
{
  int i, n;
  int l, w, c, inword;

  l = 0; // 行数
  w = 0; // 单词数
  c = 0; // 字符数
  inword = 0; // 标记是否在单词内

  // 从文件描述符循环读取数据到缓冲区
  while((n = read(fd, buf, sizeof(buf))) > 0){
    for(i=0; i<n; i++){
      c++; // 字符数加一
      if(buf[i] == '\n')
        l++; // 遇到换行符，行数加一
      
      // 判断是否为空白字符
      if(strchr(" \r\t\n\v", buf[i]))
        inword = 0; // 在空白符中，不在单词内
      else if(!inword){
        w++; // 刚进入一个新单词，单词数加一
        inword = 1; // 标记进入单词
      }
    }
  }

  // 如果读取出错
  if(n < 0){
    printf("wc: read error\n");
    exit(1);
  }
  
  // 打印结果
  printf("%d %d %d %s\n", l, w, c, name);
}

int
main(int argc, char *argv[])
{
  int fd, i;

  // 如果没有指定文件，则从标准输入读取
  if(argc <= 1){
    wc(0, "");
    exit(0);
  }

  // 循环处理所有输入文件
  for(i = 1; i < argc; i++){
    if((fd = open(argv[i], O_RDONLY)) < 0){
      printf("wc: cannot open %s\n", argv[i]);
      exit(1);
    }
    wc(fd, argv[i]);
    close(fd);
  }
  exit(0);
}
