//
// QEMU virtio 磁盘设备驱动程序。
// 本驱动使用 MMIO (Memory-Mapped I/O) 接口与 virtio 设备进行通信。
// VirtIO 是一种半虚拟化设备标准，旨在提高 I/O 性能。
//
// 启动 QEMU 的示例命令:
// qemu-system-riscv64 -drive file=fs.img,if=none,format=raw,id=x0 -device virtio-blk-device,drive=x0,bus=virtio-mmio-bus.0
//

#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "fs.h"
#include "buf.h"
#include "virtio.h"

// VIRTIO0 是 virtio 磁盘设备 MMIO 寄存器的基地址。
// 这个宏通过基地址加上偏移量来访问指定的寄存器。
#define R(r) ((volatile uint32 *)(VIRTIO0 + (r)))

// 全局唯一的磁盘状态结构体。
static struct disk {
  // virtqueue 的核心组件。virtqueue 是驱动和设备之间通信的主要机制。
  // 它由三部分组成：描述符表、可用环和已用环。

  // 1. 描述符表 (Descriptor Table)
  //    一个数组，包含了 NUM 个描述符。每个描述符指向一块内存（例如，请求头、数据缓冲区、状态字节）。
  //    多个描述符可以链接在一起，形成一个“链”，用于表示一个完整的请求。
  struct virtq_desc *desc;

  // 2. 可用环 (Available Ring)
  //    驱动程序通过这个环形缓冲区告诉设备哪些描述符链是准备好可以处理的。
  //    驱动将描述符链的头部索引写入此环中。
  struct virtq_avail *avail;

  // 3. 已用环 (Used Ring)
  //    设备通过这个环形缓冲区告诉驱动哪些描述符链已经被处理完毕。
  //    设备将处理完的描述符链的头部索引写入此环中。
  struct virtq_used *used;

  // 驱动程序内部的簿记信息。
  char free[NUM];      // 跟踪每个描述符是否空闲。1 表示空闲，0 表示正在使用。
  uint16 used_idx;     // 记录驱动已经从 used 环中处理到的位置，避免重复处理。

  // 存储每个正在进行的磁盘操作的信息。
  // 当中断发生时，驱动需要知道哪个请求完成了，以及该请求对应哪个缓冲区。
  // 这个数组通过描述符链的头部索引来查找这些信息。
  struct {
    struct buf *b;     // 指向与请求关联的缓冲区 (struct buf)。
    char status;       // 设备在处理完请求后，会写入一个状态字节。
  } info[NUM];

  // 为每个描述符预分配的磁盘请求头。
  // 这样做可以避免在每次请求时动态分配内存。
  struct virtio_blk_req ops[NUM];
  
  // 保护整个 disk 结构体的自旋锁，防止并发访问导致数据不一致。
  struct spinlock vdisk_lock;
  
} disk;

// 初始化 virtio 磁盘设备。
void
virtio_disk_init(void)
{
  uint32 status = 0;

  initlock(&disk.vdisk_lock, "virtio_disk");

  // --- VirtIO 设备初始化序列 ---
  // 根据 VirtIO 规范，驱动必须遵循一个严格的步骤来初始化设备。

  // 验证设备身份：检查 MagicValue, Version, DeviceID, VendorID。
  // 这确保我们正在与一个我们期望的 QEMU virtio 块设备通信。
  if(*R(VIRTIO_MMIO_MAGIC_VALUE) != 0x74726976 ||
     *R(VIRTIO_MMIO_VERSION) != 2 ||
     *R(VIRTIO_MMIO_DEVICE_ID) != 2 ||
     *R(VIRTIO_MMIO_VENDOR_ID) != 0x554d4551){
    panic("could not find virtio disk");
  }
  
  // 步骤 0: 重置设备。写入 0 到 status 寄存器。
  *R(VIRTIO_MMIO_STATUS) = status;

  // 步骤 1: 设置 ACKNOWLEDGE 状态位。表示驱动已识别出该设备。
  status |= VIRTIO_CONFIG_S_ACKNOWLEDGE;
  *R(VIRTIO_MMIO_STATUS) = status;

  // 步骤 2: 设置 DRIVER 状态位。表示驱动知道如何驱动该设备。
  status |= VIRTIO_CONFIG_S_DRIVER;
  *R(VIRTIO_MMIO_STATUS) = status;

  // 步骤 3: 协商特性。驱动读取设备支持的特性，然后写回驱动支持的特性子集。
  uint64 features = *R(VIRTIO_MMIO_DEVICE_FEATURES);
  // 我们只支持最基本的块设备功能，因此禁用一些高级或我们不需要的特性。
  features &= ~(1 << VIRTIO_BLK_F_RO);          // 禁用只读模式
  features &= ~(1 << VIRTIO_BLK_F_SCSI);         // 禁用 SCSI 命令
  features &= ~(1 << VIRTIO_BLK_F_CONFIG_WCE);   // 禁用写缓存配置
  features &= ~(1 << VIRTIO_BLK_F_MQ);           // 禁用多队列
  features &= ~(1 << VIRTIO_F_ANY_LAYOUT);     // 驱动不兼容任意布局
  features &= ~(1 << VIRTIO_RING_F_EVENT_IDX); // 禁用高级中断通知机制
  features &= ~(1 << VIRTIO_RING_F_INDIRECT_DESC); // 禁用间接描述符
  *R(VIRTIO_MMIO_DRIVER_FEATURES) = features;

  // 步骤 4: 设置 FEATURES_OK 状态位，告诉设备特性协商完成。
  status |= VIRTIO_CONFIG_S_FEATURES_OK;
  *R(VIRTIO_MMIO_STATUS) = status;

  // 重新读取状态，确认设备已接受我们的特性。如果未设置 FEATURES_OK，则初始化失败。
  status = *R(VIRTIO_MMIO_STATUS);
  if(!(status & VIRTIO_CONFIG_S_FEATURES_OK))
    panic("virtio disk FEATURES_OK unset");

  // 步骤 5: 初始化 virtqueue。对于块设备，通常只有一个 virtqueue (队列 0)。
  *R(VIRTIO_MMIO_QUEUE_SEL) = 0; // 选择要配置的队列

  // 确保队列尚未激活。
  if(*R(VIRTIO_MMIO_QUEUE_READY))
    panic("virtio disk should not be ready");

  // 查询设备支持的最大队列大小，并确保它不小于我们需要的 NUM。
  uint32 max = *R(VIRTIO_MMIO_QUEUE_NUM_MAX);
  if(max == 0)
    panic("virtio disk has no queue 0");
  if(max < NUM)
    panic("virtio disk max queue too short");

  // 为 virtqueue 的三个组件（描述符表、可用环、已用环）分配内存。
  // 每个组件都需要一个物理连续的页。
  disk.desc = kalloc();
  disk.avail = kalloc();
  disk.used = kalloc();
  if(!disk.desc || !disk.avail || !disk.used)
    panic("virtio disk kalloc");
  memset(disk.desc, 0, PGSIZE);
  memset(disk.avail, 0, PGSIZE);
  memset(disk.used, 0, PGSIZE);

  // 告诉设备我们将使用的队列大小。
  *R(VIRTIO_MMIO_QUEUE_NUM) = NUM;

  // 将队列内存的物理地址写入设备的 MMIO 寄存器。
  // 驱动通过这些寄存器告诉设备在哪里找到 virtqueue 的各个部分。
  *R(VIRTIO_MMIO_QUEUE_DESC_LOW) = (uint64)disk.desc;
  *R(VIRTIO_MMIO_QUEUE_DESC_HIGH) = (uint64)disk.desc >> 32;
  *R(VIRTIO_MMIO_DRIVER_DESC_LOW) = (uint64)disk.avail; // driver -> device (avail ring)
  *R(VIRTIO_MMIO_DRIVER_DESC_HIGH) = (uint64)disk.avail >> 32;
  *R(VIRTIO_MMIO_DEVICE_DESC_LOW) = (uint64)disk.used;  // device -> driver (used ring)
  *R(VIRTIO_MMIO_DEVICE_DESC_HIGH) = (uint64)disk.used >> 32;

  // 告诉设备，队列已设置好并准备就绪。
  *R(VIRTIO_MMIO_QUEUE_READY) = 0x1;

  // 将所有描述符初始化为空闲状态。
  for(int i = 0; i < NUM; i++)
    disk.free[i] = 1;

  // 步骤 6: 设置 DRIVER_OK 状态位，表示驱动已完成初始化并准备好运行。
  status |= VIRTIO_CONFIG_S_DRIVER_OK;
  *R(VIRTIO_MMIO_STATUS) = status;

  // plic.c 和 trap.c 将处理来自 VIRTIO0_IRQ 的中断。
}

// 从空闲描述符池中找到一个空闲的描述符，标记为已使用，并返回其索引。
// 如果没有可用的描述符，返回 -1。
static int
alloc_desc()
{
  for(int i = 0; i < NUM; i++){
    if(disk.free[i]){
      disk.free[i] = 0;
      return i;
    }
  }
  return -1;
}

// 释放一个描述符，将其标记为空闲，并唤醒可能正在等待描述符的进程。
static void
free_desc(int i)
{
  if(i >= NUM)
    panic("free_desc 1");
  if(disk.free[i])
    panic("free_desc 2");
  disk.desc[i].addr = 0;
  disk.desc[i].len = 0;
  disk.desc[i].flags = 0;
  disk.desc[i].next = 0;
  disk.free[i] = 1;
  wakeup(&disk.free[0]);
}

// 释放一个描述符链。
// 从链头开始，沿着 next 指针逐个释放链中的所有描述符。
static void
free_chain(int i)
{
  while(1){
    int flag = disk.desc[i].flags;
    int nxt = disk.desc[i].next;
    free_desc(i);
    if(flag & VRING_DESC_F_NEXT) // 检查是否还有下一个描述符
      i = nxt;
    else
      break;
  }
}

// 分配三个描述符用于一次磁盘操作。
// VirtIO 块设备请求固定使用三个描述符组成的链。
// 如果无法一次性分配三个，则会释放已分配的并返回-1。
static int
alloc3_desc(int *idx)
{
  for(int i = 0; i < 3; i++){
    idx[i] = alloc_desc();
    if(idx[i] < 0){
      for(int j = 0; j < i; j++)
        free_desc(idx[j]);
      return -1;
    }
  }
  return 0;
}

// 读或写一个磁盘块。
void
virtio_disk_rw(struct buf *b, int write)
{
  // xv6 的文件系统使用块号，而磁盘设备使用扇区号。
  // BSIZE 通常是 1024 字节，而扇区大小是 512 字节，所以一个块等于两个扇区。
  uint64 sector = b->blockno * (BSIZE / 512);

  acquire(&disk.vdisk_lock);

  // --- 构造一个三描述符链 ---
  // 根据 VirtIO 规范 5.2.6 节，一次标准的块请求由三个描述符组成：
  // 1. 请求头 (包含类型、扇区号等)
  // 2. 数据缓冲区 (读或写的数据)
  // 3. 状态字节 (设备写回操作结果)

  // 循环分配三个描述符。
  int idx[3];
  while(1){
    if(alloc3_desc(idx) == 0) {
      break;
    }
    // 如果没有足够的描述符，则休眠。
    // 当中断处理程序释放描述符时，会调用 wakeup() 唤醒我们。
    sleep(&disk.free[0], &disk.vdisk_lock);
  }

  // --- 填充描述符 ---

  // 描述符 0: 请求头 (device-readable)
  struct virtio_blk_req *buf0 = &disk.ops[idx[0]];
  if(write)
    buf0->type = VIRTIO_BLK_T_OUT; // 写操作
  else
    buf0->type = VIRTIO_BLK_T_IN;  // 读操作
  buf0->reserved = 0;
  buf0->sector = sector;

  disk.desc[idx[0]].addr = (uint64) buf0;
  disk.desc[idx[0]].len = sizeof(struct virtio_blk_req);
  disk.desc[idx[0]].flags = VRING_DESC_F_NEXT; // 标志位：表示此描述符链接到下一个
  disk.desc[idx[0]].next = idx[1];             // 指向链中的下一个描述符

  // 描述符 1: 数据缓冲区
  disk.desc[idx[1]].addr = (uint64) b->data;
  disk.desc[idx[1]].len = BSIZE;
  if(write)
    // 对于写操作，缓冲区是 device-readable。
    disk.desc[idx[1]].flags = 0; 
  else
    // 对于读操作，缓冲区是 device-writable。
    disk.desc[idx[1]].flags = VRING_DESC_F_WRITE; 
  disk.desc[idx[1]].flags |= VRING_DESC_F_NEXT; // 标志位：链接到下一个
  disk.desc[idx[1]].next = idx[2];              // 指向链中的下一个描述符

  // 描述符 2: 状态字节 (device-writable)
  // 初始化为一个不太可能的值 (0xff)，设备成功时会将其写入 0。
  disk.info[idx[0]].status = 0xff; 
  disk.desc[idx[2]].addr = (uint64) &disk.info[idx[0]].status;
  disk.desc[idx[2]].len = 1;
  disk.desc[idx[2]].flags = VRING_DESC_F_WRITE; // 标志位：设备将写入此描述符
  disk.desc[idx[2]].next = 0;                   // 链的末尾

  // 保存 buf 指针，以便在中断时可以唤醒正确的进程。
  b->disk = 1; // 标记 buf 正在进行磁盘 I/O
  disk.info[idx[0]].b = b;

  // --- 将请求提交给设备 ---

  // 将描述符链的头部索引放入可用环 (avail ring)。
  disk.avail->ring[disk.avail->idx % NUM] = idx[0];

  // 内存屏障，确保对描述符和可用环的所有写入都先于 idx 更新。
  __sync_synchronize();

  // 更新可用环的 idx，这会向设备暴露新的请求。
  // 注意：idx 是一个累加器，不会对 NUM 取模。
  disk.avail->idx += 1; 

  // 内存屏障，确保 idx 的更新对设备可见。
  __sync_synchronize();

  // 通知设备队列 0 有新的请求需要处理。
  *R(VIRTIO_MMIO_QUEUE_NOTIFY) = 0; // 值为要通知的队列号

  // 等待中断处理程序 (virtio_disk_intr) 完成请求并唤醒我们。
  // 当 b->disk 变为 0 时，表示 I/O 完成。
  while(b->disk == 1) {
    sleep(b, &disk.vdisk_lock);
  }

  // --- 清理工作 ---
  disk.info[idx[0]].b = 0; // 清除 info 中的 buf 指针
  free_chain(idx[0]);      // 释放整个描述符链

  release(&disk.vdisk_lock);
}

// virtio 磁盘中断处理程序。
void
virtio_disk_intr()
{
  acquire(&disk.vdisk_lock);

  // 确认中断。驱动必须向设备确认中断，否则设备不会发送新的中断。
  // 读取 INTERRUPT_STATUS 并写回 INTERRUPT_ACK 来确认中断。
  // 掩码 & 0x3 用于确认 "used buffer notification" 和 "configuration change notification"。
  *R(VIRTIO_MMIO_INTERRUPT_ACK) = *R(VIRTIO_MMIO_INTERRUPT_STATUS) & 0x3;

  // 内存屏障，确保中断确认操作在读取 used ring 之前完成。
  __sync_synchronize();

  // 循环处理所有设备已完成的请求。
  // 设备每完成一个请求，就会向 used ring 中添加一个条目，并增加 disk.used->idx。
  // 我们循环直到我们的 used_idx 赶上设备的 used->idx。
  while(disk.used_idx != disk.used->idx){
    // 内存屏障，确保在访问 used ring 内容之前，我们看到了最新的 disk.used->idx。
    __sync_synchronize();
    
    // 获取已完成的描述符链的头部索引。
    int id = disk.used->ring[disk.used_idx % NUM].id;

    // 检查状态字节。0 表示成功。
    if(disk.info[id].status != 0)
      panic("virtio_disk_intr status");
    
    // 找到与此请求关联的缓冲区。
    struct buf *b = disk.info[id].b;
    b->disk = 0;   // 标记 buf 的 I/O 操作已完成
    wakeup(b);     // 唤醒在 virtio_disk_rw 中休眠的进程

    disk.used_idx += 1; // 更新我们处理到的位置
  }

  release(&disk.vdisk_lock);
}
