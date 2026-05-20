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

#define NBUCKET 13

struct {
  struct spinlock lock;

  // Linked list of all buffers, through prev/next.
  // Sorted by how recently the buffer was used.
  // head.next is most recent, head.prev is least.
  struct buf head;
} bcache[NBUCKET];
struct buf buf[NBUF];
// The global lock to avoid steal more than one same buf
// “Optimize for the common case, serialize the rare case.”
struct spinlock glock;

void
binit(void)
{
  struct buf *b;
  initlock(&glock, "global_bcache");

  for(int i = 0; i < NBUCKET; i++) {
    initlock(&bcache[i].lock, "bcache");
    // Create linked list of buffers
    bcache[i].head.prev = &bcache[i].head;
    bcache[i].head.next = &bcache[i].head;
  }

  for(b = buf; b < buf + NBUF; b++) {
    b->next = bcache[0].head.next;
    b->prev = &bcache[0].head;
    initsleeplock(&b->lock, "buffer");
    bcache[0].head.next->prev = b;
    bcache[0].head.next       = b;
  }
}

// Look through buffer cache for block on device dev.
// If not found, allocate a buffer.
// In either case, return locked buffer.
static struct buf *
bget(uint dev, uint blockno)
{
  struct buf *b;
  uint        i = blockno % NBUCKET;
  acquire(&bcache[i].lock);

  // Is the block already cached?
  for(b = bcache[i].head.next; b != &bcache[i].head; b = b->next) {
    if(b->dev == dev && b->blockno == blockno) {
      b->refcnt++;
      release(&bcache[i].lock);
      acquiresleep(&b->lock);
      return b;
    }
  }

  // Not cached.
  // Recycle the least recently used (LRU) unused buffer.
  for(b = bcache[i].head.prev; b != &bcache[i].head; b = b->prev) {
    if(b->refcnt == 0) {
      b->dev     = dev;
      b->blockno = blockno;
      b->valid   = 0;
      b->refcnt  = 1;
      release(&bcache[i].lock);
      acquiresleep(&b->lock);
      return b;
    }
  }

  // All the bufs in the bucket are used, steel.
  release(&bcache[i].lock);
  acquire(&glock);

  // Double-check after acquisition in case other thread
  // has stolen the buf into bcachei
  acquire(&bcache[i].lock);
  for(b = bcache[i].head.next; b != &bcache[i].head; b = b->next) {
    if(b->dev == dev && b->blockno == blockno) {
      // Others has stolen the buf, release all.
      b->refcnt++;
      release(&bcache[i].lock);
      release(&glock);
      acquiresleep(&b->lock); 
      return b;
    }
  }
  release(&bcache[i].lock);

  for(int j = 0; j < NBUCKET; j++) {
    if(i == j)
      continue;
    acquire(&bcache[j].lock);
    for(b = bcache[j].head.prev; b != &bcache[j].head; b = b->prev) {
      if(b->refcnt == 0) {
        b->dev     = dev;
        b->blockno = blockno;
        b->valid   = 0;
        b->refcnt  = 1;

        // Delete from bcachej
        b->prev->next = b->next;
        b->next->prev = b->prev;
        release(&bcache[j].lock);

        // Add to bcachei
        acquire(&bcache[i].lock);
        b->next                   = bcache[i].head.next;
        b->prev                   = &bcache[i].head;
        bcache[i].head.next->prev = b;
        bcache[i].head.next       = b;
        release(&bcache[i].lock);

        release(&glock);

        acquiresleep(&b->lock);
        return b;
      }
    }
    release(&bcache[j].lock);
  }
  release(&glock);
  panic("bget: no buffers");
}

// Return a locked buf with the contents of the indicated block.
struct buf *
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
// Move to the head of the most-recently-used list.
void
brelse(struct buf *b)
{
  if(!holdingsleep(&b->lock))
    panic("brelse");

  releasesleep(&b->lock);
  uint i = b->blockno % NBUCKET;
  acquire(&bcache[i].lock);
  b->refcnt--;
  if(b->refcnt == 0) {
    // no one is waiting for it.
    b->next->prev             = b->prev;
    b->prev->next             = b->next;
    b->next                   = bcache[i].head.next;
    b->prev                   = &bcache[i].head;
    bcache[i].head.next->prev = b;
    bcache[i].head.next       = b;
  }

  release(&bcache[i].lock);
}

void
bpin(struct buf *b)
{
  uint i = b->blockno % NBUCKET;
  acquire(&bcache[i].lock);
  b->refcnt++;
  release(&bcache[i].lock);
}

void
bunpin(struct buf *b)
{
  uint i = b->blockno % NBUCKET;
  acquire(&bcache[i].lock);
  b->refcnt--;
  release(&bcache[i].lock);
}
