// Buffer cache.
//
// The buffer cache is a linked list of buf structures holding
// cached copies of disk block contents.  Caching disk blocks
// in memory reduces the number of disk reads and also provides
// a synchronization point for disk blocks used by multiple processes.
//
// Interface:
// * To get a buffer for a particular disk block, call bread.
// * After changing buffer data, call bwrite to write it to disk.
// * When done with the buffer, call brelse.
// * Do not use the buffer after calling brelse.
// * Only one process at a time can use a buffer,
//     so do not keep them longer than necessary.


#include "types.h"
#include "param.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "riscv.h"
#include "defs.h"
#include "fs.h"
#include "buf.h"

#define NBUCKETS 13  // Use a prime number to reduce hash conflicts

struct bucket {
  struct spinlock lock;
  struct buf head;  // Head of linked list for this bucket
};

struct {
  struct spinlock lock;  // Global lock for buffer allocation
  struct buf buf[NBUF];
  struct bucket buckets[NBUCKETS];
} bcache;

// Hash function to map (dev, blockno) to bucket
static int
hash(uint dev, uint blockno)
{
  return (dev + blockno) % NBUCKETS;
}

void
binit(void)
{
  struct buf *b;
  char lockname[16];

  initlock(&bcache.lock, "bcache");

  // Initialize bucket locks and lists
  for(int i = 0; i < NBUCKETS; i++) {
    snprintf(lockname, sizeof(lockname), "bcache.bucket");
    initlock(&bcache.buckets[i].lock, lockname);
    
    // Initialize empty circular list for each bucket
    bcache.buckets[i].head.prev = &bcache.buckets[i].head;
    bcache.buckets[i].head.next = &bcache.buckets[i].head;
  }

  // Initialize all buffers and add them to bucket 0 initially
  for(b = bcache.buf; b < bcache.buf+NBUF; b++){
    initsleeplock(&b->lock, "buffer");
    
    // Add to bucket 0 initially
    b->next = bcache.buckets[0].head.next;
    b->prev = &bcache.buckets[0].head;
    bcache.buckets[0].head.next->prev = b;
    bcache.buckets[0].head.next = b;
  }
}

// Look through buffer cache for block on device dev.
// If not found, allocate a buffer.
// In either case, return locked buffer.
static struct buf*
bget(uint dev, uint blockno)
{
  struct buf *b;
  int bucket_idx = hash(dev, blockno);
  struct bucket *bucket = &bcache.buckets[bucket_idx];

  acquire(&bucket->lock);

  // Is the block already cached in this bucket?
  for(b = bucket->head.next; b != &bucket->head; b = b->next){
    if(b->dev == dev && b->blockno == blockno){
      b->refcnt++;
      release(&bucket->lock);
      acquiresleep(&b->lock);
      return b;
    }
  }

  // Not cached. Look for an unused buffer first in this bucket.
  for(b = bucket->head.prev; b != &bucket->head; b = b->prev){
    if(b->refcnt == 0) {
      b->dev = dev;
      b->blockno = blockno;
      b->valid = 0;
      b->refcnt = 1;
      release(&bucket->lock);
      acquiresleep(&b->lock);
      return b;
    }
  }
  release(&bucket->lock);

  // No unused buffer in target bucket. Look in all buckets.
  // This part is serialized to avoid complex deadlock scenarios.
  acquire(&bcache.lock);
  
  // Check target bucket again (someone might have freed a buffer)
  acquire(&bucket->lock);
  for(b = bucket->head.prev; b != &bucket->head; b = b->prev){
    if(b->refcnt == 0) {
      b->dev = dev;
      b->blockno = blockno;
      b->valid = 0;
      b->refcnt = 1;
      release(&bucket->lock);
      release(&bcache.lock);
      acquiresleep(&b->lock);
      return b;
    }
  }
  release(&bucket->lock);

  // Look for unused buffer in other buckets
  for(int i = 0; i < NBUCKETS; i++) {
    if(i == bucket_idx) continue;
    
    struct bucket *other_bucket = &bcache.buckets[i];
    acquire(&other_bucket->lock);
    
    for(b = other_bucket->head.prev; b != &other_bucket->head; b = b->prev){
      if(b->refcnt == 0) {
        // Remove from current bucket
        b->next->prev = b->prev;
        b->prev->next = b->next;
        release(&other_bucket->lock);
        
        // Add to target bucket
        acquire(&bucket->lock);
        b->next = bucket->head.next;
        b->prev = &bucket->head;
        bucket->head.next->prev = b;
        bucket->head.next = b;
        
        b->dev = dev;
        b->blockno = blockno;
        b->valid = 0;
        b->refcnt = 1;
        release(&bucket->lock);
        release(&bcache.lock);
        acquiresleep(&b->lock);
        return b;
      }
    }
    release(&other_bucket->lock);
  }
  
  release(&bcache.lock);
  panic("bget: no buffers");
}

// Return a locked buf with the contents of the indicated block.
struct buf*
bread(uint dev, uint blockno)
{
  struct buf *b;

  b = bget(dev, blockno);
  if(!b->valid) {
    virtio_disk_rw(b, 0);
    b->valid = 1;
  }
  return b;
}

// Write b's contents to disk.  Must be locked.
void
bwrite(struct buf *b)
{
  if(!holdingsleep(&b->lock))
    panic("bwrite");
  virtio_disk_rw(b, 1);
}

// Release a locked buffer.
void
brelse(struct buf *b)
{
  if(!holdingsleep(&b->lock))
    panic("brelse");

  releasesleep(&b->lock);

  int bucket_idx = hash(b->dev, b->blockno);
  struct bucket *bucket = &bcache.buckets[bucket_idx];
  
  acquire(&bucket->lock);
  b->refcnt--;
  release(&bucket->lock);
}

void
bpin(struct buf *b) {
  int bucket_idx = hash(b->dev, b->blockno);
  struct bucket *bucket = &bcache.buckets[bucket_idx];
  
  acquire(&bucket->lock);
  b->refcnt++;
  release(&bucket->lock);
}

void
bunpin(struct buf *b) {
  int bucket_idx = hash(b->dev, b->blockno);
  struct bucket *bucket = &bcache.buckets[bucket_idx];
  
  acquire(&bucket->lock);
  b->refcnt--;
  release(&bucket->lock);
}


