#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "param.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "fs.h"
#include "buf.h"

// 简单的日志记录，允许多个文件系统系统调用并发执行。
//
// 日志系统的核心思想：
// 1. **事务（Transaction）**: 一个事务由一个或多个文件系统操作（如创建文件、写入数据）组成。
//    例如，创建一个文件可能需要修改目录的数据块和 inode 块。这两个修改就属于同一个事务。
// 2. **预写式日志（Write-ahead Logging）**: 在将数据块实际写入其在文件系统中的最终位置之前，
//    首先将所有修改记录在一个专门的日志区域（on-disk log）。
// 3. **提交（Commit）**: 当一个事务的所有日志记录都成功写入磁盘后，该事务被认为是“已提交”的。
//    通过在日志的头部记录一个特殊的提交记录来标记。
// 4. **安装（Install）**: 提交后，再将日志中的数据块复制到它们在文件系统中的最终位置。
// 5. **恢复（Recovery）**: 如果在安装过程中系统崩溃，重启后，系统会检查日志。
//    如果日志中有已提交但未完全安装的事务，系统会重新执行安装过程，保证数据的一致性。
//
// xv6 的日志系统简化了这一过程：
// - **原子性单元**: 每个系统调用被视为一个原子操作单元。
// - **批量提交**: 日志系统会等待所有当前正在执行的文件系统调用都结束后，才进行一次性的提交。
//   这避免了处理部分完成的系统调用的复杂性。
// - **磁盘布局**: 日志在磁盘上由一个头块（header block）和多个数据块组成。
//   - 头块：记录了当前事务修改了哪些磁盘块（通过块号）。
//   - 数据块：按顺序存放着被修改块的新内容。
//
// 函数调用流程:
// a. 文件系统操作开始: `begin_op()`
// b. 修改数据块: `log_write()` (替换 `bwrite()`)
// c. 文件系统操作结束: `end_op()` -> `commit()` -> `write_log()` -> `write_head()` -> `install_trans()` -> `write_head()`

// logheader 结构定义了日志头部的格式。
// 它保存在磁盘日志区的第一个块中。
// 同时，在内存中的 `log.lh` 字段也使用这个结构来缓存当前事务的信息。
struct logheader {
  int n;  // 当前事务中记录的块的数量。
  int block[LOGSIZE]; // 记录被修改的块的块号。LOGSIZE 是日志所能容纳的最大块数。
};

// log 结构定义了整个日志系统的状态。
struct log {
  struct spinlock lock; // 保护日志系统数据结构的自旋锁。
  int start;            // 日志区在磁盘上的起始块号。
  int size;             // 日志区的总大小（以块为单位）。
  int outstanding;      // 正在进行的（尚未调用 end_op 的）文件系统系统调用的数量。
  int committing;       // 标志位，如果非零，表示日志系统正在提交事务。
  int dev;              // 文件系统所在的设备号。
  struct logheader lh;  // 内存中缓存的日志头，记录当前事务的信息。
};
struct log log; // 全局唯一的日志系统实例。

// 内部函数声明
static void recover_from_log(void);
static void commit();

// 初始化日志系统。
// 在 `fsinit()` 中被调用。
void
initlog(int dev, struct superblock *sb)
{
  if (sizeof(struct logheader) >= BSIZE)
    panic("initlog: logheader struct too large for a block");

  initlock(&log.lock, "log");
  log.start = sb->logstart;
  log.size = sb->nlog;
  log.dev = dev;
  // 从日志中恢复，以处理上次系统崩溃时可能存在的未完成的事务。
  recover_from_log();
}

// 将已提交事务的块从日志区复制到它们在文件系统中的最终位置。
// 这个过程被称为“安装事务”。
static void
install_trans(int recovering) // recovering 参数表示是否在恢复模式下调用
{
  int tail;

  for (tail = 0; tail < log.lh.n; tail++) {
    // 从日志区读取一个数据块
    struct buf *lbuf = bread(log.dev, log.start + tail + 1);
    // 读取该数据块对应的在文件系统中的目标块
    struct buf *dbuf = bread(log.dev, log.lh.block[tail]);
    // 将日志中的内容复制到目标块的缓冲区
    memmove(dbuf->data, lbuf->data, BSIZE);
    // 将目标块写回磁盘，完成安装
    bwrite(dbuf);
    // 如果不是在恢复期间，需要 unpin 目标缓冲区。
    // 在恢复期间，没有其他进程会使用这些块，所以不需要 unpin。
    if(recovering == 0)
      bunpin(dbuf);
    brelse(lbuf); // 释放日志块缓冲区
    brelse(dbuf); // 释放目标块缓冲区
  }
}

// 从磁盘读取日志头到内存中的 `log.lh`。
static void
read_head(void)
{
  // 读取日志区的第一个块，即头块
  struct buf *buf = bread(log.dev, log.start);
  struct logheader *lh = (struct logheader *) (buf->data);
  int i;
  log.lh.n = lh->n; // 复制事务中的块数量
  // 复制所有块号
  for (i = 0; i < log.lh.n; i++) {
    log.lh.block[i] = lh->block[i];
  }
  brelse(buf); // 释放头块缓冲区
}

// 将内存中的 `log.lh` 写回磁盘上的日志头块。
// 这是事务提交的关键步骤。一旦头块被成功写入磁盘，
// 即使系统崩溃，重启后也能通过 `recover_from_log` 恢复事务。
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
  bwrite(buf); // 将修改后的头块写回磁盘
  brelse(buf);
}

// 从日志中恢复。
// 在 `initlog` 时调用，用于处理上次运行可能未完成的事务。
static void
recover_from_log(void)
{
  // 读取上次的日志头
  read_head();
  // 如果上次有已提交的事务 (log.lh.n > 0)，则安装它
  install_trans(1);
  // 清空日志头，表示没有待处理的事务了
  log.lh.n = 0;
  // 将清空后的日志头写回磁盘
  write_head();
}

// 每个文件系统系统调用的开始时必须调用此函数。
void
begin_op(void)
{
  acquire(&log.lock);
  while(1){
    // 如果日志系统正在提交，当前操作必须等待。
    if(log.committing){
      sleep(&log, &log.lock);
    }
    // 检查日志空间是否足够。
    // 这是一个保守的估计：假设当前操作和所有其他正在进行的操作
    // 都会达到它们的最大块写入数 (MAXOPBLOCKS)。
    else if(log.lh.n + (log.outstanding + 1) * MAXOPBLOCKS > LOGSIZE){
      // 日志空间可能不足，等待下一次提交以释放空间。
      sleep(&log, &log.lock);
    } else {
      // 空间足够，增加正在进行的操作计数，然后返回。
      log.outstanding += 1;
      release(&log.lock);
      break;
    }
  }
}

// 每个文件系统系统调用的结束时必须调用此函数。
void
end_op(void)
{
  int do_commit = 0;

  acquire(&log.lock);
  // 减少正在进行的操作计数
  log.outstanding -= 1;
  if(log.committing)
    panic("end_op: log is committing");
  // 如果这是最后一个正在进行的操作，则触发提交。
  if(log.outstanding == 0){
    do_commit = 1;
    log.committing = 1; // 设置提交标志，阻止新的操作开始
  } else {
    // 如果还有其他操作正在进行，唤醒可能在 begin_op() 中等待的进程。
    // 因为当前操作结束，释放了一些预留的日志空间。
    wakeup(&log);
  }
  release(&log.lock);

  if(do_commit){
    // 调用 commit() 时不能持有锁，因为它内部会调用 sleep。
    commit();
    // 提交完成后，重置提交标志并唤醒所有等待的进程。
    acquire(&log.lock);
    log.committing = 0;
    wakeup(&log);
    release(&log.lock);
  }
}

// 将缓存中已修改的块的内容写入到磁盘上的日志区。
static void
write_log(void)
{
  int tail;

  for (tail = 0; tail < log.lh.n; tail++) {
    // to: 日志区中的目标块
    struct buf *to = bread(log.dev, log.start + tail + 1);
    // from: 内存缓冲区中被修改的块
    struct buf *from = bread(log.dev, log.lh.block[tail]);
    // 将数据从内存缓冲区复制到日志块
    memmove(to->data, from->data, BSIZE);
    bwrite(to);  // 将日志块写入磁盘
    brelse(from);
    brelse(to);
  }
}

// 执行事务提交。
static void
commit()
{
  if (log.lh.n > 0) {
    // 步骤 1: 将所有修改过的块从缓冲区写入日志区 (write-ahead)
    write_log();
    // 步骤 2: 将日志头写入磁盘。这是“提交点”。
    // 一旦这步完成，即使系统崩溃，事务也可以被恢复。
    write_head();
    // 步骤 3: 将日志中的块安装到它们在文件系统中的最终位置。
    install_trans(0);
    // 步骤 4: 清空内存中的日志头，为下一个事务做准备。
    log.lh.n = 0;
    // 步骤 5: 将清空的日志头写入磁盘，表示当前没有待处理的事务。
    // 这样可以防止系统在此时崩溃后，重启时错误地重放同一个事务。
    write_head();
  }
}

// 当调用者修改了一个缓冲区并希望将其写入磁盘时，应调用此函数。
// `log_write` 会记录被修改块的块号，并将其固定在缓冲区缓存中，
// 以防止它被驱逐，直到事务提交。
//
// `log_write()` 替代了 `bwrite()`。典型用法如下:
//   bp = bread(...)       // 读取块
//   (修改 bp->data)       // 修改数据
//   log_write(bp)         // 将修改记录到日志
//   brelse(bp)            // 释放缓冲区
void
log_write(struct buf *b)
{
  int i;

  acquire(&log.lock);
  // 检查日志是否已满。
  if (log.lh.n >= LOGSIZE || log.lh.n >= log.size - 1)
    panic("log_write: too big a transaction");
  // 必须在 begin_op/end_op 对之间调用 log_write。
  if (log.outstanding < 1)
    panic("log_write: outside of transaction");

  // "日志吸收" (Log Absorption):
  // 如果这个块已经被记录在当前事务中，我们不需要再次添加它。
  // 只需确保最新的修改被写入即可。
  for (i = 0; i < log.lh.n; i++) {
    if (log.lh.block[i] == b->blockno) {
      break;
    }
  }
  // 记录块号。
  log.lh.block[i] = b->blockno;
  // 如果这是一个新的块（之前未被记录在本次事务中），
  // 则增加日志中的块计数，并钉住（pin）该缓冲区。
  if (i == log.lh.n) {
    bpin(b);
    log.lh.n++;
  }
  release(&log.lock);
}
