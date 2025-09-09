// Physical memory allocator, for user processes,
// kernel stacks, page-table pages,
// and pipe buffers. Allocates whole 4096-byte pages.

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "riscv.h"
#include "defs.h"

void freerange(void *pa_start, void *pa_end);

extern char end[]; // first address after kernel.
                   // defined by kernel.ld.

struct run {
  struct run *next;
};

struct {
  struct spinlock lock;
  struct run *freelist;
} kmem;

#ifdef LAB_PGTBL
// superpage freelist
struct superrun {
  struct superrun *next;
  void *pa;
};
struct {
  struct spinlock lock;
  struct superrun *freelist;
} kmem_super;
#endif

void
kinit()
{
  initlock(&kmem.lock, "kmem");
  // Reserve a small tail region for superpages and avoid adding it
  // to the normal 4KB freelist. Reserve up to 8 superpages at top of RAM.
#ifdef LAB_PGTBL
  initlock(&kmem_super.lock, "kmem_super");
  // compute reservation region
  uint64 super_region_size = 8 * SUPERPGSIZE;
  void *super_region_start = (void*)(PHYSTOP - super_region_size);
  if((void*)end < super_region_start) {
    freerange(end, super_region_start);
    // add super blocks
    for(uint64 p = (uint64)super_region_start; p + SUPERPGSIZE <= PHYSTOP; p += SUPERPGSIZE){
      struct superrun *sr = (struct superrun*)p;
      // don't touch memory contents; just add to freelist
      acquire(&kmem_super.lock);
      sr->pa = (void*)p;
      sr->next = kmem_super.freelist;
      kmem_super.freelist = sr;
      release(&kmem_super.lock);
    }
    // no tail after reserved region
  } else {
    // not enough space; fall back to normal freelist for all
    freerange(end, (void*)PHYSTOP);
  }
#else
  freerange(end, (void*)PHYSTOP);
#endif
}

void
freerange(void *pa_start, void *pa_end)
{
  char *p;
  p = (char*)PGROUNDUP((uint64)pa_start);
  for(; p + PGSIZE <= (char*)pa_end; p += PGSIZE)
    kfree(p);
}

// Free the page of physical memory pointed at by pa,
// which normally should have been returned by a
// call to kalloc().  (The exception is when
// initializing the allocator; see kinit above.)
void
kfree(void *pa)
{
  struct run *r;

  if(((uint64)pa % PGSIZE) != 0 || (char*)pa < end || (uint64)pa >= PHYSTOP)
    panic("kfree");

  // Fill with junk to catch dangling refs.
  memset(pa, 1, PGSIZE);

  r = (struct run*)pa;

  acquire(&kmem.lock);
  r->next = kmem.freelist;
  kmem.freelist = r;
  release(&kmem.lock);
}

// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.
void *
kalloc(void)
{
  struct run *r;

  acquire(&kmem.lock);
  r = kmem.freelist;
  if(r)
    kmem.freelist = r->next;
  release(&kmem.lock);

  if(r)
    memset((char*)r, 5, PGSIZE); // fill with junk
  return (void*)r;
}

#ifdef LAB_PGTBL
// Allocate one superpage (2MB) region. Returns physical address or 0.
void *
superalloc(void)
{
  struct superrun *sr;
  acquire(&kmem_super.lock);
  sr = kmem_super.freelist;
  if(sr)
    kmem_super.freelist = sr->next;
  release(&kmem_super.lock);
  if(sr)
    return sr->pa;
  return 0;
}

// Free a superpage back to the superpage freelist.
void
superfree(void *pa)
{
  struct superrun *sr = (struct superrun*)pa;
  acquire(&kmem_super.lock);
  sr->pa = pa;
  sr->next = kmem_super.freelist;
  kmem_super.freelist = sr;
  release(&kmem_super.lock);
}
#endif
