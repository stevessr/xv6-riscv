// 睡眠锁（Long-term locks for processes）
// 当进程需要持有锁相当长一段时间时，应使用睡眠锁。
// 睡眠锁在等待时会放弃CPU，允许其他进程运行。
// 这与自旋锁形成对比，自旋锁会一直“旋转”直到锁被释放，期间会独占CPU。
struct sleeplock {
  uint locked;       // 锁是否被持有 (0 表示未被持有, 1 表示被持有)
  struct spinlock lk; // 用于保护此睡眠锁的自旋锁
                     // 在检查或修改 sleeplock 状态时，必须持有这个自旋锁
                     // 以防止多个进程同时操作 sleeplock 导致竞态条件。

  // 用于调试:
  char *name;        // 锁的名称，方便调试时识别
  int pid;           // 持有该锁的进程ID (Process ID)
};
