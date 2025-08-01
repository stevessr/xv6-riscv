//
// virtio 设备接口定义。
// 本文件包含了 virtio MMIO (Memory-Mapped I/O) 接口的寄存器定义
// 以及 virtio 描述符表的结构，这些都是 virtio 设备通信的核心。
// 这些定义主要基于 QEMU 的实现，并遵循 virtio v1.1 规范。
//
// virtio 规范 v1.1:
// https://docs.oasis-open.org/virtio/virtio/v1.1/virtio-v1.1.pdf
//

// virtio MMIO 控制寄存器，映射到物理地址 0x10001000 开始的内存区域。
// 寄存器偏移量定义，源自 QEMU (hw/virtio/virtio-mmio.h)。
#define VIRTIO_MMIO_MAGIC_VALUE		0x000 // 魔数，固定为 0x74726976 ('virt' in little-endian)
#define VIRTIO_MMIO_VERSION		0x004 // virtio 版本号，驱动应检查其是否为 2
#define VIRTIO_MMIO_DEVICE_ID		0x008 // 设备类型 ID (1: net, 2: block, etc.)
#define VIRTIO_MMIO_VENDOR_ID		0x00c // 厂商 ID，QEMU 的为 0x554d4551 ('QEMU' in little-endian)
#define VIRTIO_MMIO_DEVICE_FEATURES	0x010 // 设备提供的特性位掩码
#define VIRTIO_MMIO_DRIVER_FEATURES	0x020 // 驱动程序接受的特性位掩码
#define VIRTIO_MMIO_QUEUE_SEL		0x030 // 选择要操作的虚拟队列 (只写)
#define VIRTIO_MMIO_QUEUE_NUM_MAX	0x034 // 所选队列支持的最大描述符数量 (只读)
#define VIRTIO_MMIO_QUEUE_NUM		0x038 // 驱动为所选队列分配的描述符数量 (只写)
#define VIRTIO_MMIO_QUEUE_READY		0x044 // 队列就绪位 (读/写)，用于启动队列
#define VIRTIO_MMIO_QUEUE_NOTIFY	0x050 // 通知设备有新的缓冲区待处理 (只写)
#define VIRTIO_MMIO_INTERRUPT_STATUS	0x060 // 中断状态 (只读)
#define VIRTIO_MMIO_INTERRUPT_ACK	0x064 // 中断应答 (只写)
#define VIRTIO_MMIO_STATUS		0x070 // 设备状态 (读/写)
#define VIRTIO_MMIO_QUEUE_DESC_LOW	0x080 // 描述符表的物理地址低 32 位 (只写)
#define VIRTIO_MMIO_QUEUE_DESC_HIGH	0x084 // 描述符表的物理地址高 32 位 (只写)
#define VIRTIO_MMIO_DRIVER_DESC_LOW	0x090 // 可用环 (Driver Area) 的物理地址低 32 位 (只写)
#define VIRTIO_MMIO_DRIVER_DESC_HIGH	0x094 // 可用环 (Driver Area) 的物理地址高 32 位 (只写)
#define VIRTIO_MMIO_DEVICE_DESC_LOW	0x0a0 // 已用环 (Device Area) 的物理地址低 32 位 (只写)
#define VIRTIO_MMIO_DEVICE_DESC_HIGH	0x0a4 // 已用环 (Device Area) 的物理地址高 32 位 (只写)

// 设备状态寄存器 (VIRTIO_MMIO_STATUS) 的位定义。
// 这些位用于驱动与设备之间的初始化握手过程。
#define VIRTIO_CONFIG_S_ACKNOWLEDGE	1 // 驱动已识别设备
#define VIRTIO_CONFIG_S_DRIVER		2 // 驱动已准备好
#define VIRTIO_CONFIG_S_DRIVER_OK	4 // 驱动设置成功，设备可用
#define VIRTIO_CONFIG_S_FEATURES_OK	8 // 特性协商完成

// 设备特性位 (VIRTIO_MMIO_DEVICE_FEATURES)。
#define VIRTIO_BLK_F_RO              5	/* 磁盘为只读设备 */
#define VIRTIO_BLK_F_SCSI            7	/* 支持 SCSI 命令透传 */
#define VIRTIO_BLK_F_CONFIG_WCE     11	/* 支持配置写缓存 */
#define VIRTIO_BLK_F_MQ             12	/* 支持多队列 */
#define VIRTIO_F_ANY_LAYOUT         27  /* 驱动接受任意内存布局 */
#define VIRTIO_RING_F_INDIRECT_DESC 28  /* 支持间接描述符 */
#define VIRTIO_RING_F_EVENT_IDX     29  /* 支持事件索引，用于抑制中断 */

// 每个虚拟队列的描述符数量。
// 该值必须是 2 的幂。
#define NUM 8

// 单个 virtio 描述符结构。
// 描述符用于定义一个供设备访问的内存缓冲区。
struct virtq_desc {
  uint64 addr;  // 缓冲区的物理地址
  uint32 len;   // 缓冲区的长度 (字节)
  uint16 flags; // 描述符标志
  uint16 next;  // 链接到下一个描述符的索引 (如果设置了 F_NEXT 标志)
};
#define VRING_DESC_F_NEXT  1 // 表示该描述符链接到 'next' 字段指定的另一个描述符
#define VRING_DESC_F_WRITE 2 // 表示该缓冲区是设备可写的 (否则为设备只读)

// 可用环 (Available Ring)，由驱动程序填充，用于向设备提供新的缓冲区。
struct virtq_avail {
  uint16 flags;       // 标志，用于控制中断通知
  uint16 idx;         // 驱动程序下一次将在 ring[idx % NUM] 中放置新的描述符链头
  uint16 ring[NUM];   // 描述符链的头部索引数组
  uint16 used_event;  // (如果 VIRTIO_F_EVENT_IDX) 驱动请求设备在处理到此索引时通知
};

// 已用环 (Used Ring) 中的一个条目，由设备填充，
// 用于通知驱动程序已经处理完一个缓冲区。
struct virtq_used_elem {
  uint32 id;   // 已处理完成的描述符链的头部索引
  uint32 len;  // 写入缓冲区的总长度
};

// 已用环 (Used Ring) 结构，设备用它来通知驱动程序已完成的请求。
struct virtq_used {
  uint16 flags;       // 标志，用于控制中断通知
  uint16 idx;         // 设备下一次将在 ring[idx % NUM] 中放置新的已用条目
  struct virtq_used_elem ring[NUM];
  uint16 avail_event; // (如果 VIRTIO_F_EVENT_IDX) 设备请求驱动在可用环到达此索引时通知
};

// 以下定义特定于 virtio 块设备 (如 virtio-blk 磁盘)。
// 详见 virtio 规范的 5.2 节。

#define VIRTIO_BLK_T_IN  0 // 读操作 (设备 -> 内存)
#define VIRTIO_BLK_T_OUT 1 // 写操作 (内存 -> 设备)

// virtio 块设备请求的头部结构。
// 一个典型的块设备请求由三个描述符组成：
// 1. 请求头 (本结构)
// 2. 数据缓冲区 (读或写)
// 3. 一个单字节的状态，由设备写回
struct virtio_blk_req {
  uint32 type;     // 请求类型: VIRTIO_BLK_T_IN 或 VIRTIO_BLK_T_OUT
  uint32 reserved; // 保留字段
  uint64 sector;   // 要操作的扇区号
};
