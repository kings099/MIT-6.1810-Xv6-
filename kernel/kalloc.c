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
// Superpage allocator
struct superrun {
  struct superrun *next;
};

struct {
  struct spinlock lock;
  struct superrun *freelist;
  int count;  // number of available superpages
} super_kmem;

#define NSUPERPAGES 8  // number of superpages to set aside
#endif

void
kinit()
{
  initlock(&kmem.lock, "kmem");
#ifdef LAB_PGTBL
  initlock(&super_kmem.lock, "super_kmem");
  super_kmem.freelist = 0;
  super_kmem.count = 0;
  
  // Reserve some 2MB-aligned regions for superpages
  // Start from a 2MB-aligned address and set aside NSUPERPAGES regions
  uint64 super_start = SUPERPGROUNDUP((uint64)end);
  uint64 super_end = super_start + NSUPERPAGES * SUPERPGSIZE;
  
  // Make sure we don't exceed PHYSTOP
  if(super_end <= PHYSTOP) {
    // Initialize superpage free list
    for(uint64 pa = super_start; pa < super_end; pa += SUPERPGSIZE) {
      struct superrun *r = (struct superrun*)pa;
      r->next = super_kmem.freelist;
      super_kmem.freelist = r;
      super_kmem.count++;
    }
    // Free the remaining memory to normal allocator, skipping superpage area
    freerange(end, (void*)super_start);
    freerange((void*)super_end, (void*)PHYSTOP);
  } else {
    // Not enough space for superpages, fall back to normal allocation
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
// Allocate one 2MB superpage of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.
void *
superalloc(void)
{
  struct superrun *r;

  acquire(&super_kmem.lock);
  r = super_kmem.freelist;
  if(r) {
    super_kmem.freelist = r->next;
    super_kmem.count--;
  }
  release(&super_kmem.lock);

  if(r)
    memset((char*)r, 5, SUPERPGSIZE); // fill with junk
  return (void*)r;
}

// Free the 2MB superpage of physical memory pointed at by pa.
void
superfree(void *pa)
{
  struct superrun *r;

  if(((uint64)pa % SUPERPGSIZE) != 0 || (char*)pa < end || (uint64)pa >= PHYSTOP)
    panic("superfree");

  // Fill with junk to catch dangling refs.
  memset(pa, 1, SUPERPGSIZE);

  r = (struct superrun*)pa;

  acquire(&super_kmem.lock);
  r->next = super_kmem.freelist;
  super_kmem.freelist = r;
  super_kmem.count++;
  release(&super_kmem.lock);
}
#endif
