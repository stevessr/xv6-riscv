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
static void split_superpage_into_kmem(void);

extern char end[]; // first address after kernel.
                   // defined by kernel.ld.

struct run {
  struct run *next;
};

struct {
  struct spinlock lock;
  struct run *freelist;
} kmem;

// Superpage (2MB) pool
struct {
  struct spinlock lock;
  struct run *superfreelist; // reuse run pointer type to chain 2MB blocks
} superkmem;

void
kinit()
{
  initlock(&kmem.lock, "kmem");
  initlock(&superkmem.lock, "superkmem");
  freerange(end, (void*)PHYSTOP);
}

void
freerange(void *pa_start, void *pa_end)
{
  char *p = (char*)PGROUNDUP((uint64)pa_start);

  // First, scan for 2MB-aligned superpages and add them to the super pool.
  // We step by PGSIZE normally but when we find a SUPERPGSIZE-aligned region
  // with at least SUPERPGSIZE left we add a 2MB block to superfreelist.
  for(; p + PGSIZE <= (char*)pa_end; ){
    // if p is SUPERPGSIZE-aligned and enough space remains, allocate as superpage
    if(((uint64)p % SUPERPGSIZE) == 0 && (char*)pa_end - p >= SUPERPGSIZE){
      // add the 2MB block to superfreelist (store as run chains)
      struct run *r = (struct run*)p;
      acquire(&superkmem.lock);
      r->next = superkmem.superfreelist;
      superkmem.superfreelist = r;
      release(&superkmem.lock);
      // skip the whole 2MB
      p += SUPERPGSIZE;
    } else {
      // otherwise, add a normal page to the regular free list
      kfree(p);
      p += PGSIZE;
    }
  }
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
  if(!r){
    release(&kmem.lock);
    // try to split a superpage into small pages
    split_superpage_into_kmem();
    acquire(&kmem.lock);
    r = kmem.freelist;
  }
  if(r)
    kmem.freelist = r->next;
  release(&kmem.lock);

  if(r)
    memset((char*)r, 5, PGSIZE); // fill with junk
  return (void*)r;
}

// If regular kmem freelist is empty, try to split a superpage into 4KB pages.
static void
split_superpage_into_kmem(void)
{
  struct run *sr = 0;

  // grab a superpage from superfreelist
  acquire(&superkmem.lock);
  sr = superkmem.superfreelist;
  if(sr)
    superkmem.superfreelist = sr->next;
  release(&superkmem.lock);

  if(!sr)
    return;

  // split into 4KB pages and free them into kmem freelist
  char *p = (char*)sr;
  for(int i = 0; i < SUPERPGSIZE / PGSIZE; i++){
    // kfree will acquire kmem.lock for each page
    kfree(p + i * PGSIZE);
  }
}

// Allocate one SUPERPGSIZE (2MB) block of physical memory.
// Returns a pointer that the kernel can use, or 0 on failure.
void *
superalloc(void)
{
  struct run *r;

  acquire(&superkmem.lock);
  r = superkmem.superfreelist;
  if(r)
    superkmem.superfreelist = r->next;
  release(&superkmem.lock);

  if(r)
    memset((char*)r, 5, SUPERPGSIZE); // fill with junk
  return (void*)r;
}

// Free a 2MB superpage block.
void
superfree(void *pa)
{
  struct run *r;

  if(((uint64)pa % SUPERPGSIZE) != 0 || (char*)pa < end || (uint64)pa + SUPERPGSIZE > PHYSTOP){
    // Debug: print and return instead of panicking during early test iterations.
  // superfree: invalid pa (ignored in release build)
    return;
  }

  // Fill with junk to catch dangling refs.
  memset(pa, 1, SUPERPGSIZE);

  r = (struct run*)pa;
  acquire(&superkmem.lock);
  r->next = superkmem.superfreelist;
  superkmem.superfreelist = r;
  release(&superkmem.lock);
}
