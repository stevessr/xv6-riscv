//
// qemu的virtio磁盘设备驱动程序。
// 使用qemu的mmio接口与virtio交互。
//
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

// virtio mmio寄存器r的地址。
#define R(r) ((volatile uint32 *)(VIRTIO0 + (r)))

static struct disk {
  // 一组（而不是一个环）DMA描述符，驱动程序通过它
  // 告诉设备在哪里读写单个磁盘操作。
  // 有NUM个描述符。
  // 大多数命令由几个这些描述符组成的“链”（链表）构成。
  struct virtq_desc *desc;

  // 一个环，驱动程序在其中写入驱动程序希望设备处理的描述符编号。
  // 它只包括每个链的头描述符。该环有NUM个元素。
  struct virtq_avail *avail;

  // 一个环，设备在其中写入设备已完成处理的描述符编号（仅每个链的头部）。
  // 有NUM个已使用的环条目。
  struct virtq_used *used;

  // 我们自己的簿记。
  char free[NUM];  // 描述符是否空闲？
  uint16 used_idx; // 我们已经检查到used[2..NUM]的这个位置。

  // 跟踪正在进行的操作的信息，
  // 供完成中断到达时使用。
  // 按链的第一个描述符索引进行索引。
  struct {
    struct buf *b;
    char status;
  } info[NUM];

  // 磁盘命令头。
  // 为方便起见，与描述符一一对应。
  struct virtio_blk_req ops[NUM];
  
  struct spinlock vdisk_lock;
  
} disk;

void
virtio_disk_init(void)
{
  uint32 status = 0;

  initlock(&disk.vdisk_lock, "virtio_disk");

  if(*R(VIRTIO_MMIO_MAGIC_VALUE) != 0x74726976 ||
     *R(VIRTIO_MMIO_VERSION) != 2 ||
     *R(VIRTIO_MMIO_DEVICE_ID) != 2 ||
     *R(VIRTIO_MMIO_VENDOR_ID) != 0x554d4551){
    panic("could not find virtio disk");
  }
  
  // 重置设备
  *R(VIRTIO_MMIO_STATUS) = status;

  // 设置ACKNOWLEDGE状态位
  status |= VIRTIO_CONFIG_S_ACKNOWLEDGE;
  *R(VIRTIO_MMIO_STATUS) = status;

  // 设置DRIVER状态位
  status |= VIRTIO_CONFIG_S_DRIVER;
  *R(VIRTIO_MMIO_STATUS) = status;

  // 协商功能
  uint64 features = *R(VIRTIO_MMIO_DEVICE_FEATURES);
  features &= ~(1 << VIRTIO_BLK_F_RO);
  features &= ~(1 << VIRTIO_BLK_F_SCSI);
  features &= ~(1 << VIRTIO_BLK_F_CONFIG_WCE);
  features &= ~(1 << VIRTIO_BLK_F_MQ);
  features &= ~(1 << VIRTIO_F_ANY_LAYOUT);
  features &= ~(1 << VIRTIO_RING_F_EVENT_IDX);
  features &= ~(1 << VIRTIO_RING_F_INDIRECT_DESC);
  *R(VIRTIO_MMIO_DRIVER_FEATURES) = features;

  // 告诉设备功能协商已完成。
  status |= VIRTIO_CONFIG_S_FEATURES_OK;
  *R(VIRTIO_MMIO_STATUS) = status;

  // 重新读取状态以确保FEATURES_OK已设置。
  status = *R(VIRTIO_MMIO_STATUS);
  if(!(status & VIRTIO_CONFIG_S_FEATURES_OK))
    panic("virtio disk FEATURES_OK unset");

  // 初始化队列0。
  *R(VIRTIO_MMIO_QUEUE_SEL) = 0;

  // 确保队列0未使用。
  if(*R(VIRTIO_MMIO_QUEUE_READY))
    panic("virtio disk should not be ready");

  // 检查最大队列大小。
  uint32 max = *R(VIRTIO_MMIO_QUEUE_NUM_MAX);
  if(max == 0)
    panic("virtio disk has no queue 0");
  if(max < NUM)
    panic("virtio disk max queue too short");

  // 分配并清零队列内存。
  disk.desc = kalloc();
  disk.avail = kalloc();
  disk.used = kalloc();
  if(!disk.desc || !disk.avail || !disk.used)
    panic("virtio disk kalloc");
  memset(disk.desc, 0, PGSIZE);
  memset(disk.avail, 0, PGSIZE);
  memset(disk.used, 0, PGSIZE);

  // 设置队列大小。
  *R(VIRTIO_MMIO_QUEUE_NUM) = NUM;

  // 写入物理地址。
  *R(VIRTIO_MMIO_QUEUE_DESC_LOW) = (uint64)disk.desc;
  *R(VIRTIO_MMIO_QUEUE_DESC_HIGH) = (uint64)disk.desc >> 32;
  *R(VIRTIO_MMIO_DRIVER_DESC_LOW) = (uint64)disk.avail;
  *R(VIRTIO_MMIO_DRIVER_DESC_HIGH) = (uint64)disk.avail >> 32;
  *R(VIRTIO_MMIO_DEVICE_DESC_LOW) = (uint64)disk.used;
  *R(VIRTIO_MMIO_DEVICE_DESC_HIGH) = (uint64)disk.used >> 32;

  // 队列已准备好。
  *R(VIRTIO_MMIO_QUEUE_READY) = 0x1;

  // 所有NUM个描述符最初都未使用。
  for(int i = 0; i < NUM; i++)
    disk.free[i] = 1;

  // 告诉设备我们已完全准备好。
  status |= VIRTIO_CONFIG_S_DRIVER_OK;
  *R(VIRTIO_MMIO_STATUS) = status;

  // plic.c和trap.c安排来自VIRTIO0_IRQ的中断。
}

// 查找一个空闲描述符，将其标记为非空闲，并返回其索引。
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

// 将描述符标记为空闲。
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

// 分配三个描述符（它们不必是连续的）。
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
  uint64 sector = b->blockno * (BSIZE / 512);

  acquire(&disk.vdisk_lock);

  // 规范的5.2节说，传统的块操作使用
  // 三个描述符：一个用于类型/保留/扇区，一个用于
  // 数据，一个用于1字节的状态结果。

  // 分配三个描述符。
  int idx[3];
  while(1){
    if(alloc3_desc(idx) == 0) {
      break;
    }
    sleep(&disk.free[0], &disk.vdisk_lock);
  }

  // 格式化三个描述符。
  // qemu的virtio-blk.c读取它们。

  struct virtio_blk_req *buf0 = &disk.ops[idx[0]];

  if(write)
    buf0->type = VIRTIO_BLK_T_OUT; // 写入磁盘
  else
    buf0->type = VIRTIO_BLK_T_IN; // 读取磁盘
  buf0->reserved = 0;
  buf0->sector = sector;

  disk.desc[idx[0]].addr = (uint64) buf0;
  disk.desc[idx[0]].len = sizeof(struct virtio_blk_req);
  disk.desc[idx[0]].flags = VRING_DESC_F_NEXT;
  disk.desc[idx[0]].next = idx[1];

  disk.desc[idx[1]].addr = (uint64) b->data;
  disk.desc[idx[1]].len = BSIZE;
  if(write)
    disk.desc[idx[1]].flags = 0; // 设备读取b->data
  else
    disk.desc[idx[1]].flags = VRING_DESC_F_WRITE; // 设备写入b->data
  disk.desc[idx[1]].flags |= VRING_DESC_F_NEXT;
  disk.desc[idx[1]].next = idx[2];

  disk.info[idx[0]].status = 0xff; // 设备成功时写入0
  disk.desc[idx[2]].addr = (uint64) &disk.info[idx[0]].status;
  disk.desc[idx[2]].len = 1;
  disk.desc[idx[2]].flags = VRING_DESC_F_WRITE; // 设备写入状态
  disk.desc[idx[2]].next = 0;

  // 为virtio_disk_intr()记录struct buf。
  b->disk = 1;
  disk.info[idx[0]].b = b;

  // 告诉设备我们描述符链中的第一个索引。
  disk.avail->ring[disk.avail->idx % NUM] = idx[0];

  __sync_synchronize();

  // 告诉设备另一个可用的环条目可用。
  disk.avail->idx += 1; // 不是 % NUM ...

  __sync_synchronize();

  *R(VIRTIO_MMIO_QUEUE_NOTIFY) = 0; // 值是队列号

  // 等待virtio_disk_intr()表示请求已完成。
  while(b->disk == 1) {
    sleep(b, &disk.vdisk_lock);
  }

  disk.info[idx[0]].b = 0;
  free_chain(idx[0]);

  release(&disk.vdisk_lock);
}

void
virtio_disk_intr()
{
  acquire(&disk.vdisk_lock);

  // 在我们告诉设备我们已经看到这个中断之前，
  // 设备不会引发另一个中断，下面这行代码就是这样做的。
  // 这可能会与设备向“used”环写入新条目发生竞争，
  // 在这种情况下，我们可能会在此中断中处理新的完成条目，
  // 而在下一次中断中无事可做，这是无害的。
  *R(VIRTIO_MMIO_INTERRUPT_ACK) = *R(VIRTIO_MMIO_INTERRUPT_STATUS) & 0x3;

  __sync_synchronize();

  // 当设备向used环添加条目时，它会增加disk.used->idx。

  while(disk.used_idx != disk.used->idx){
    __sync_synchronize();
    int id = disk.used->ring[disk.used_idx % NUM].id;

    if(disk.info[id].status != 0)
      panic("virtio_disk_intr status");

    struct buf *b = disk.info[id].b;
    b->disk = 0;   // 磁盘已处理完buf
    wakeup(b);

    disk.used_idx += 1;
  }

  release(&disk.vdisk_lock);
}