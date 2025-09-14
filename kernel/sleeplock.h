//-*- coding: utf-8 -*-
// 进程的长期锁
struct sleeplock
{
  uint locked;        // 锁是否被持有？
  struct spinlock lk; // 保护此睡眠锁的自旋锁

  // 用于调试:
  char *name; // 锁的名称。
  int pid;    // 持有锁的进程
};