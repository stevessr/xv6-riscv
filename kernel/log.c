//-*- coding: utf-8 -*-
#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "param.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "fs.h"
#include "buf.h"

// 简单的日志记录，允许并发的文件系统系统调用。
//
// 一个日志事务包含多个文件系统系统调用的更新。
// 日志系统仅在没有活动的文件系统系统调用时才提交。
// 因此，永远不需要考虑提交是否可能将未提交的系统调用的更新写入磁盘。
//
// 系统调用应调用 begin_op()/end_op() 来标记其开始和结束。
// 通常 begin_op() 只是增加正在进行的文件系统系统调用的计数并返回。
// 但是，如果它认为日志接近耗尽，它会休眠直到最后一个未完成的 end_op() 提交。
//
// 日志是包含磁盘块的物理重做日志。
// 磁盘上的日志格式：
//   头块，包含块A、B、C等的块号
//   块A
//   块B
//   块C
//   ...
// 日志追加是同步的。

// 头块的内容，用于磁盘上的头块和在提交前在内存中跟踪记录的块号。
struct logheader {
  int n;
  int block[LOGBLOCKS];
};

struct log {
  struct spinlock lock;
  int start;
  int outstanding; // 正在执行的文件系统系统调用数。
  int committing;  // 正在 commit() 中，请等待。
  int dev;
  struct logheader lh;
};
struct log log;

static void recover_from_log(void);
static void commit();

void
initlog(int dev, struct superblock *sb)
{
  if (sizeof(struct logheader) >= BSIZE)
    panic("initlog: too big logheader");

  initlock(&log.lock, "log");
  log.start = sb->logstart;
  log.dev = dev;
  recover_from_log();
}

// 将已提交的块从日志复制到其最终位置
static void
install_trans(int recovering)
{
  int tail;

  for (tail = 0; tail < log.lh.n; tail++) {
    if(recovering) {
      printf("recovering tail %d dst %d\n", tail, log.lh.block[tail]);
    }
    struct buf *lbuf = bread(log.dev, log.start+tail+1); // 读取日志块
    struct buf *dbuf = bread(log.dev, log.lh.block[tail]); // 读取目标块
    memmove(dbuf->data, lbuf->data, BSIZE);  // 将块复制到目标
    bwrite(dbuf);  // 将目标块写入磁盘
    if(recovering == 0)
      bunpin(dbuf);
    brelse(lbuf);
    brelse(dbuf);
  }
}

// 从磁盘读取日志头到内存中的日志头
static void
read_head(void)
{
  struct buf *buf = bread(log.dev, log.start);
  struct logheader *lh = (struct logheader *) (buf->data);
  int i;
  log.lh.n = lh->n;
  for (i = 0; i < log.lh.n; i++) {
    log.lh.block[i] = lh->block[i];
  }
  brelse(buf);
}

// 将内存中的日志头写入磁盘。
// 这是当前事务提交的真正时刻。
static void
write_head(void)
{
  struct buf *buf = bread(log.dev, log.start);
  struct logheader *hb = (struct logheader *) (buf->data);
  int i;
  hb->n = log.lh.n;
  for (i = 0; i < log.lh.n; i++) {
    hb->block[i] = log.lh.block[i];
  }
  bwrite(buf);
  brelse(buf);
}

static void
recover_from_log(void)
{
  read_head();
  install_trans(1); // 如果已提交，则从日志复制到磁盘
  log.lh.n = 0;
  write_head(); // 清除日志
}

// 在每个文件系统系统调用开始时调用。
void
begin_op(void)
{
  acquire(&log.lock);
  while(1){
    if(log.committing){
      sleep(&log, &log.lock);
    } else if(log.lh.n + (log.outstanding+1)*MAXOPBLOCKS > LOGBLOCKS){
      // 这个操作可能会耗尽日志空间；等待提交。
      sleep(&log, &log.lock);
    } else {
      log.outstanding += 1;
      release(&log.lock);
      break;
    }
  }
}

// 在每个文件系统系统调用结束时调用。
// 如果这是最后一个未完成的操作，则提交。
void
end_op(void)
{
  int do_commit = 0;

  acquire(&log.lock);
  log.outstanding -= 1;
  if(log.committing)
    panic("log.committing");
  if(log.outstanding == 0){
    do_commit = 1;
    log.committing = 1;
  } else {
    // begin_op() 可能正在等待日志空间，
    // 并且减少 log.outstanding 已经减少了
    // 保留的空间量。
    wakeup(&log);
  }
  release(&log.lock);

  if(do_commit){
    // 在不持有锁的情况下调用 commit，因为不允许
    // 在持有锁的情况下休眠。
    commit();
    acquire(&log.lock);
    log.committing = 0;
    wakeup(&log);
    release(&log.lock);
  }
}

// 将修改后的块从缓存复制到日志。
static void
write_log(void)
{
  int tail;

  for (tail = 0; tail < log.lh.n; tail++) {
    struct buf *to = bread(log.dev, log.start+tail+1); // 日志块
    struct buf *from = bread(log.dev, log.lh.block[tail]); // 缓存块
    memmove(to->data, from->data, BSIZE);
    bwrite(to);  // 写入日志
    brelse(from);
    brelse(to);
  }
}

static void
commit()
{
  if (log.lh.n > 0) {
    write_log();     // 将修改后的块从缓存写入日志
    write_head();    // 将头写入磁盘 -- 真正的提交
    install_trans(0); // 现在将写入安装到其最终位置
    log.lh.n = 0;
    write_head();    // 从日志中擦除事务
  }
}

// 调用者已经修改了 b->data 并完成了对缓冲区的操作。
// 记录块号并通过增加 refcnt 将其固定在缓存中。
// commit()/write_log() 将执行磁盘写入。
//
// log_write() 替换了 bwrite(); 一个典型的用法是：
//   bp = bread(...)
//   修改 bp->data[]
//   log_write(bp)
//   brelse(bp)
void
log_write(struct buf *b)
{
  int i;

  acquire(&log.lock);
  if (log.lh.n >= LOGBLOCKS)
    panic("too big a transaction");
  if (log.outstanding < 1)
    panic("log_write outside of trans");

  for (i = 0; i < log.lh.n; i++) {
    if (log.lh.block[i] == b->blockno)   // 日志吸收
      break;
  }
  log.lh.block[i] = b->blockno;
  if (i == log.lh.n) {  // 向日志添加新块？
    bpin(b);
    log.lh.n++;
  }
  release(&log.lock);
}