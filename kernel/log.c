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
// 日志系统只在没有活动的文件系统系统调用时才提交。
// 因此，永远不需要考虑提交是否会
// 将未提交的系统调用的更新写入磁盘。
//
// 系统调用应该调用 begin_op()/end_op() 来标记
// 其开始和结束。通常 begin_op() 只是增加
// 进行中的文件系统系统调用的计数并返回。
// 但是，如果它认为日志即将用完，它会
// 休眠直到最后一个未完成的 end_op() 提交。
//
// 日志是包含磁盘块的物理重做日志。
// 磁盘上的日志格式：
//  头块，包含块 A, B, C, ... 的块号
//  块 A
//  块 B
//  块 C
//  ...
// 日志追加是同步的。

// 头块的内容，用于磁盘上的头块
// 以及在提交前在内存中跟踪记录的块号。
struct logheader {
  int n;
  int block[LOGSIZE];
};

struct log {
  struct spinlock lock;
  int start;         // 日志区在磁盘上的起始块号
  int size;          // 日志区的总块数
  int outstanding;   // 有多少个文件系统系统调用正在执行。
  int committing;    // 正在提交中，请等待。
  int dev;           // 设备号
  struct logheader lh; // 内存中的日志头
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
  log.size = sb->nlog;
  log.dev = dev;
  recover_from_log(); // 从日志中恢复
}

// 将已提交的块从日志复制到它们的目标位置
static void
install_trans(int recovering)
{
  int tail;

  for (tail = 0; tail < log.lh.n; tail++) {
    struct buf *lbuf = bread(log.dev, log.start+tail+1); // 读取日志块
    struct buf *dbuf = bread(log.dev, log.lh.block[tail]); // 读取目标块
    memmove(dbuf->data, lbuf->data, BSIZE);  // 将块复制到目标
    bwrite(dbuf);  // 将目标块写入磁盘
    if(recovering == 0)
      bunpin(dbuf); // 如果不是在恢复，则取消固定缓冲区
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

// 从日志中恢复
static void
recover_from_log(void)
{
  read_head();
  install_trans(1); // 如果已提交，则从日志复制到磁盘
  log.lh.n = 0;
  write_head(); // 清除日志
}

// 在每个文件系统系统调用的开始处调用。
void
begin_op(void)
{
  acquire(&log.lock);
  while(1){
    if(log.committing){ // 如果正在提交，则等待
      sleep(&log, &log.lock);
    } else if(log.lh.n + (log.outstanding+1)*MAXOPBLOCKS > LOGSIZE){
      // 这个操作可能会耗尽日志空间；等待提交。
      sleep(&log, &log.lock);
    } else {
      log.outstanding += 1;
      release(&log.lock);
      break;
    }
  }
}

// 在每个文件系统系统调用的结束处调用。
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
    // 而减少 log.outstanding 减少了保留的空间。
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

// 将修改过的块从缓存复制到日志。
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

// 提交事务
static void
commit()
{
  if (log.lh.n > 0) {
    write_log();     // 将修改过的块从缓存写入日志
    write_head();    // 将头写入磁盘 -- 真正的提交
    install_trans(0); // 现在将写入安装到目标位置
    log.lh.n = 0;
    write_head();    // 从日志中擦除事务
  }
}

// 调用者修改了 b->data 并完成了对缓冲区的操作。
// 记录块号并通过增加 refcnt 在缓存中固定它。
// commit()/write_log() 将执行磁盘写入。
//
// log_write() 替换 bwrite()；一个典型的用法是：
//   bp = bread(...)
//   修改 bp->data[]
//   log_write(bp)
//   brelse(bp)
void
log_write(struct buf *b)
{
  int i;

  acquire(&log.lock);
  if (log.lh.n >= LOGSIZE || log.lh.n >= log.size - 1)
    panic("too big a transaction");
  if (log.outstanding < 1)
    panic("log_write outside of trans");

  for (i = 0; i < log.lh.n; i++) {
    if (log.lh.block[i] == b->blockno)   // 日志吸收 (log absorption)
      break;
  }
  log.lh.block[i] = b->blockno;
  if (i == log.lh.n) {  // 是否向日志添加新块？
    bpin(b);
    log.lh.n++;
  }
  release(&log.lock);
}
