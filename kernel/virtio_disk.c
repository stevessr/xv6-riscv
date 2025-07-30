//
// QEMU 的 virtio 磁盘设备驱动程序。
// 使用 QEMU 的 MMIO (内存映射 I/O) 接口与 virtio 进行交互。
//
// 启动 QEMU 的命令示例:
// qemu ... -drive file=fs.img,if=none,format=raw,id=x0 -device virtio-blk-device,drive=x0,bus=virtio-mmio-bus.0
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

// 获取 virtio mmio 寄存器 r 的地址。
#define R(r) ((volatile uint32 *)(VIRTIO0 + (r)))

// 磁盘状态结构体
static struct disk {
  // 一组 DMA 描述符（descriptors），驱动通过它们告诉设备在哪里读写单个磁盘操作。
  // 总共有 NUM 个描述符。
  // 大多数命令由一个包含几个描述符的“链”（链表）组成。
  struct virtq_desc *desc;

  // 一个环形缓冲区（avail ring），驱动在其中写入希望设备处理的描述符编号。
  // 它只包含每个链的头部描述符。该环有 NUM 个元素。
  struct virtq_avail *avail;

  // 一个环形缓冲区（used ring），设备在其中写入已完成处理的描述符编号（同样只是链的头部）。
  // 有 NUM 个已用环形条目。
  struct virtq_used *used;

  // 我们自己的簿记信息。
  char free[NUM];  // 标记一个描述符是否空闲。
  uint16 used_idx; // 我们已经在 used 环中检查到的位置。

  // 跟踪正在进行的磁盘操作的信息，以便在完成中断到达时使用。
  // 按链的第一个描述符的索引进行索引。
  struct {
    struct buf *b;
    char status;
  } info[NUM];

  // 磁盘命令头。
  // 为方便起见，与描述符一一对应。
  struct virtio_blk_req ops[NUM];
  
  // 保护磁盘状态的自旋锁
  struct spinlock vdisk_lock;
  
} disk;

void
virtio_disk_init(void)
{
  uint32 status = 0;

  initlock(&disk.vdisk_lock, "virtio_disk");

  // 检查 MagicValue, Version, DeviceID 和 VendorID 以确认是 virtio 磁盘设备。
  if(*R(VIRTIO_MMIO_MAGIC_VALUE) != 0x74726976 ||
     *R(VIRTIO_MMIO_VERSION) != 2 ||
     *R(VIRTIO_MMIO_DEVICE_ID) != 2 ||
     *R(VIRTIO_MMIO_VENDOR_ID) != 0x554d4551){
    panic("could not find virtio disk");
  }
  
  // 重置设备
  *R(VIRTIO_MMIO_STATUS) = status;

  // 步骤 1: 设置 ACKNOWLEDGE 状态位，表示我们已识别设备。
  status |= VIRTIO_CONFIG_S_ACKNOWLEDGE;
  *R(VIRTIO_MMIO_STATUS) = status;

  // 步骤 2: 设置 DRIVER 状态位，表示我们知道如何驱动它。
  status |= VIRTIO_CONFIG_S_DRIVER;
  *R(VIRTIO_MMIO_STATUS) = status;

  // 步骤 3: 与设备协商特性。我们只接受最基本的特性。
  uint64 features = *R(VIRTIO_MMIO_DEVICE_FEATURES);
  features &= ~(1 << VIRTIO_BLK_F_RO);
  features &= ~(1 << VIRTIO_BLK_F_SCSI);
  features &= ~(1 << VIRTIO_BLK_F_CONFIG_WCE);
  features &= ~(1 << VIRTIO_BLK_F_MQ);
  features &= ~(1 << VIRTIO_F_ANY_LAYOUT);
  features &= ~(1 << VIRTIO_RING_F_EVENT_IDX);
  features &= ~(1 << VIRTIO_RING_F_INDIRECT_DESC);
  *R(VIRTIO_MMIO_DRIVER_FEATURES) = features;

  // 步骤 4: 告诉设备特性协商完成。
  status |= VIRTIO_CONFIG_S_FEATURES_OK;
  *R(VIRTIO_MMIO_STATUS) = status;

  // 重新读取状态以确保 FEATURES_OK 已被设备接受。
  status = *R(VIRTIO_MMIO_STATUS);
  if(!(status & VIRTIO_CONFIG_S_FEATURES_OK))
    panic("virtio disk FEATURES_OK unset");

  // 步骤 5: 初始化 virtqueue 0。
  *R(VIRTIO_MMIO_QUEUE_SEL) = 0;

  // 确保队列 0 尚未使用。
  if(*R(VIRTIO_MMIO_QUEUE_READY))
    panic("virtio disk should not be ready");

  // 检查设备支持的最大队列大小。
  uint32 max = *R(VIRTIO_MMIO_QUEUE_NUM_MAX);
  if(max == 0)
    panic("virtio disk has no queue 0");
  if(max < NUM)
    panic("virtio disk max queue too short");

  // 分配并清零队列所需的内存 (描述符、可用环、已用环)。
  disk.desc = kalloc();
  disk.avail = kalloc();
  disk.used = kalloc();
  if(!disk.desc || !disk.avail || !disk.used)
    panic("virtio disk kalloc");
  memset(disk.desc, 0, PGSIZE);
  memset(disk.avail, 0, PGSIZE);
  memset(disk.used, 0, PGSIZE);

  // 设置我们想使用的队列大小。
  *R(VIRTIO_MMIO_QUEUE_NUM) = NUM;

  // 将队列内存的物理地址写入设备的 MMIO 寄存器。
  *R(VIRTIO_MMIO_QUEUE_DESC_LOW) = (uint64)disk.desc;
  *R(VIRTIO_MMIO_QUEUE_DESC_HIGH) = (uint64)disk.desc >> 32;
  *R(VIRTIO_MMIO_DRIVER_DESC_LOW) = (uint64)disk.avail; // driver -> device
  *R(VIRTIO_MMIO_DRIVER_DESC_HIGH) = (uint64)disk.avail >> 32;
  *R(VIRTIO_MMIO_DEVICE_DESC_LOW) = (uint64)disk.used; // device -> driver
  *R(VIRTIO_MMIO_DEVICE_DESC_HIGH) = (uint64)disk.used >> 32;

  // 告诉设备，队列已准备就绪。
  *R(VIRTIO_MMIO_QUEUE_READY) = 0x1;

  // 将所有 NUM 个描述符初始化为空闲状态。
  for(int i = 0; i < NUM; i++)
    disk.free[i] = 1;

  // 步骤 6: 告诉设备驱动已完全准备就绪。
  status |= VIRTIO_CONFIG_S_DRIVER_OK;
  *R(VIRTIO_MMIO_STATUS) = status;

  // plic.c 和 trap.c 会安排处理来自 VIRTIO0_IRQ 的中断。
}

// 找到一个空闲的描述符，将其标记为非空闲，并返回其索引。
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

// 将一个描述符标记为空闲。
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
static void
free_chain(int i)
{
  while(1){
    int flag = disk.desc[i].flags;
    int nxt = disk.desc[i].next;
    free_desc(i);
    if(flag & VRING_DESC_F_NEXT)
      i = nxt;
    else
      break;
  }
}

// 分配三个描述符（它们不需要是连续的）。
// 磁盘传输总是使用三个描述符。
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

void
virtio_disk_rw(struct buf *b, int write)
{
  // 将块号转换为扇区号
  uint64 sector = b->blockno * (BSIZE / 512);

  acquire(&disk.vdisk_lock);

  // VirtIO 规范的 5.2 节指出，传统的块操作使用三个描述符：
  // 1. 请求头 (类型/保留/扇区号)
  // 2. 数据缓冲区
  // 3. 状态字节 (设备写回)

  // 分配三个描述符。
  int idx[3];
  while(1){
    if(alloc3_desc(idx) == 0) {
      break;
    }
    // 如果没有足够的描述符，就休眠，等待中断处理函数释放一些。
    sleep(&disk.free[0], &disk.vdisk_lock);
  }

  // 格式化这三个描述符。
  // qemu 的 virtio-blk.c 会读取它们。

  // 描述符 0: 请求头
  struct virtio_blk_req *buf0 = &disk.ops[idx[0]];

  if(write)
    buf0->type = VIRTIO_BLK_T_OUT; // 写盘
  else
    buf0->type = VIRTIO_BLK_T_IN;  // 读盘
  buf0->reserved = 0;
  buf0->sector = sector;

  disk.desc[idx[0]].addr = (uint64) buf0;
  disk.desc[idx[0]].len = sizeof(struct virtio_blk_req);
  disk.desc[idx[0]].flags = VRING_DESC_F_NEXT; // 链接到下一个描述符
  disk.desc[idx[0]].next = idx[1];

  // 描述符 1: 数据缓冲区
  disk.desc[idx[1]].addr = (uint64) b->data;
  disk.desc[idx[1]].len = BSIZE;
  if(write)
    disk.desc[idx[1]].flags = 0; // 设备从 b->data 读取
  else
    disk.desc[idx[1]].flags = VRING_DESC_F_WRITE; // 设备写入 b->data
  disk.desc[idx[1]].flags |= VRING_DESC_F_NEXT; // 链接到下一个描述符
  disk.desc[idx[1]].next = idx[2];

  // 描述符 2: 状态
  disk.info[idx[0]].status = 0xff; // 设备成功时会写入0
  disk.desc[idx[2]].addr = (uint64) &disk.info[idx[0]].status;
  disk.desc[idx[2]].len = 1;
  disk.desc[idx[2]].flags = VRING_DESC_F_WRITE; // 设备写入状态
  disk.desc[idx[2]].next = 0;

  // 保存 buf 指针，供中断处理函数使用。
  b->disk = 1; // 标记 buf 正在进行磁盘 I/O
  disk.info[idx[0]].b = b;

  // 将我们链中的第一个描述符的索引放入 avail 环。
  disk.avail->ring[disk.avail->idx % NUM] = idx[0];

  // 内存屏障，确保之前的写入对设备可见。
  __sync_synchronize();

  // 更新 avail 环的 idx，告诉设备有一个新的可用条目。
  // 注意：idx 不对 NUM 取模，它是一个累加器。
  disk.avail->idx += 1; 

  // 内存屏障，确保 idx 的更新对设备可见。
  __sync_synchronize();

  // 通知设备队列 0 有新的请求。
  *R(VIRTIO_MMIO_QUEUE_NOTIFY) = 0; // 值为队列号

  // 等待 virtio_disk_intr() 通知请求已完成。
  while(b->disk == 1) {
    sleep(b, &disk.vdisk_lock);
  }

  // 清理
  disk.info[idx[0]].b = 0;
  free_chain(idx[0]);

  release(&disk.vdisk_lock);
}

void
virtio_disk_intr()
{
  acquire(&disk.vdisk_lock);

  // 告诉设备我们已经看到了这个中断，设备在此之后才会再次触发中断。
  // 这可以防止中断风暴。
  // 这可能会与设备写入新的“已用”环条目发生竞争，
  // 在这种情况下，我们可能会在此次中断中处理新的完成条目，
  // 并在下一次中断中无事可做，这是无害的。
  *R(VIRTIO_MMIO_INTERRUPT_ACK) = *R(VIRTIO_MMIO_INTERRUPT_STATUS) & 0x3;

  __sync_synchronize();

  // 当设备向 used 环添加条目时，会增加 disk.used->idx。
  // 我们循环处理所有新的已完成请求。
  while(disk.used_idx != disk.used->idx){
    __sync_synchronize();
    int id = disk.used->ring[disk.used_idx % NUM].id;

    if(disk.info[id].status != 0)
      panic("virtio_disk_intr status");

    struct buf *b = disk.info[id].b;
    b->disk = 0;   // 标记 buf 的磁盘 I/O 已完成
    wakeup(b);     // 唤醒在 virtio_disk_rw 中等待的进程

    disk.used_idx += 1;
  }

  release(&disk.vdisk_lock);
}
