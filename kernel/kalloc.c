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

// Reference count for each physical page
struct {
  struct spinlock lock;
  int cnt[PHYSTOP / PGSIZE];  // one ref count per page
} ref;

// Get the index in the reference count array for a physical address
int pa2idx(uint64 pa) {
  return pa / PGSIZE;
}

// Increment reference count for a page
void kref(void *pa) {
  if(((uint64)pa % PGSIZE) != 0 || (char*)pa < end || (uint64)pa >= PHYSTOP)
    panic("kref");
  
  acquire(&ref.lock);
  ref.cnt[pa2idx((uint64)pa)]++;
  release(&ref.lock);
}

// Get reference count for a page
int kgetref(void *pa) {
  if(((uint64)pa % PGSIZE) != 0 || (char*)pa < end || (uint64)pa >= PHYSTOP)
    return 0;
  
  acquire(&ref.lock);
  int cnt = ref.cnt[pa2idx((uint64)pa)];
  release(&ref.lock);
  return cnt;
}

void
kinit()
{
  initlock(&kmem.lock, "kmem");
  initlock(&ref.lock, "ref");
  freerange(end, (void*)PHYSTOP);
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

  // Decrement reference count
  acquire(&ref.lock);
  int idx = pa2idx((uint64)pa);
  if(ref.cnt[idx] > 1) {
    ref.cnt[idx]--;
    release(&ref.lock);
    return;  // Page still has references, don't free
  }
  ref.cnt[idx] = 0;
  release(&ref.lock);

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

  if(r) {
    memset((char*)r, 5, PGSIZE); // fill with junk
    // Set reference count to 1
    acquire(&ref.lock);
    ref.cnt[pa2idx((uint64)r)] = 1;
    release(&ref.lock);
  }
  return (void*)r;
}
