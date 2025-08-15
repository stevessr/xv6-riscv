#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/param.h"

#define MAX_LINE_LEN 512 // 定义每行输入的最大长度

int
main(int argc, char *argv[])
{
  char *command_argv[MAXARG]; // 用于存储要执行的命令及其参数
  char line[MAX_LINE_LEN];    // 用于存储从标准输入读取的每一行
  int i;

  // 检查命令行参数是否���够。xargs 至少需要一个命令作为参数。
  if (argc < 2) {
    fprintf(2, "Usage: xargs command [args...]\n");
    exit(1);
  }

  // 将 xargs 的命令行参数（要执行的命令及其参数）复制到 command_argv 中。
  // argv[0] 是 "xargs" 本身，所以我们从 argv[1] 开始。
  // command_argv[0] 将是命令，即 argv[1]。
  // command_argv[1] 将是 argv[2]，依此类推。
  for (i = 1; i < argc; i++) {
    command_argv[i-1] = argv[i];
  }
  // 这是将从标准输入读取的行放置在 command_argv 中的索���位置。
  int line_arg_index = argc - 1;

  // 循环从标准输入读取行，直到 gets 返回 0 (EOF)。
  while (gets(line, sizeof(line))) {
    // gets() 读取的行包含换行符，我们需要将其移除。
    if (line[strlen(line) - 1] == '\n') {
      line[strlen(line) - 1] = 0;
    }

    // 创建一个子���程来执行命令。
    if (fork() == 0) {
      // 子进程
      // 将从标准输入读取的行作为最后一个参数添加到命令��数列表中。
      command_argv[line_arg_index] = line;
      // 使用 NULL 终止参数列表，这是 exec 所要求的。
      command_argv[line_arg_index + 1] = 0; 
      
      // 执行命令。command_argv[0] 是命令，command_argv 是完整的参数列表。
      exec(command_argv[0], command_argv);
      
      // 如果 exec 返回，说明��失败了。
      fprintf(2, "exec %s failed\n", command_argv[0]);
      exit(1);
    } else {
      // 父进程
      // 等待子进程执行完毕。
      wait(0);
    }
  }

  // 正常退出。
  exit(0);
}