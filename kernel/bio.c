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

// xyf
#define NBUCKETS 13

inline uint myhash(uint blockno){
  return blockno % NBUCKETS;
}

struct {
  // xyf
  struct spinlock lock[NBUCKETS];
  struct buf buf[NBUF];

  // Linked list of all buffers, through prev/next.
  // Sorted by how recently the buffer was used.
  // head.next is most recent, head.prev is least.
  // struct buf head;
  struct buf head[NBUCKETS];
} bcache;

void
binit(void)
{
  struct buf *b;

  // xyf
  for(int i = 0; i < NBUCKETS; i++){
    initlock(&bcache.lock[i], "bcache");
    // Create linked list of buffers
    bcache.head[i].prev = &bcache.head[i];
    bcache.head[i].next = &bcache.head[i];
  }
  for(b = bcache.buf; b < bcache.buf+NBUF; b++){
    b->next = bcache.head[0].next;
    b->prev = &bcache.head[0];
    initsleeplock(&b->lock, "buffer");
    bcache.head[0].next->prev = b;
    bcache.head[0].next = b;
  }
}

// xyf
// Extract and return available cache block from bucket b.
struct buf *
bfind(int i, int needlock){
  if(needlock)
    acquire(&bcache.lock[i]);
  for(struct buf *b = bcache.head[i].prev; b != &bcache.head[i]; b = b->prev){
    if(b->refcnt == 0){
      b->prev->next = b->next;
      b->next->prev = b->prev;
      b->prev = b->next = 0;
      if(needlock)
        release(&bcache.lock[i]);
      return b;
    }
  }
  if(needlock)
    release(&bcache.lock[i]);
  return 0;
}

// Look through buffer cache for block on device dev.
// If not found, allocate a buffer.
// In either case, return locked buffer.
static struct buf*
bget(uint dev, uint blockno)
{
  struct buf *b;

  // xyf
  uint hashid = myhash(blockno);
  acquire(&bcache.lock[hashid]);

  // Is the block already cached?
  for(b = bcache.head[hashid].next; b != &bcache.head[hashid]; b = b->next){
    if(b->dev == dev && b->blockno == blockno){
      b->refcnt++;
      release(&bcache.lock[hashid]);
      acquiresleep(&b->lock);
      return b;
    }
  }

  // Not cached.
  // Recycle the least recently used (LRU) unused buffer.
  // xyf
  b = bfind(hashid, 0);
  if(!b)
    for(int i = (hashid + 1) % NBUCKETS; i != hashid; i = (i + 1) % NBUCKETS)
      if((b = bfind(i, 1)))  break;
  if(b){
    b->next = bcache.head[hashid].next;
    b->prev = &bcache.head[hashid];
    bcache.head[hashid].next->prev = b;
    bcache.head[hashid].next = b;

    b->dev = dev;
    b->blockno = blockno;
    b->valid = 0;
    b->refcnt = 1;
    release(&bcache.lock[hashid]);
    acquiresleep(&b->lock);
    return b;
  }
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
// Move to the head of the most-recently-used list.
void
brelse(struct buf *b)
{
  if(!holdingsleep(&b->lock))
    panic("brelse");

  releasesleep(&b->lock);

  // xyf
  uint hashid = myhash(b->blockno);
  acquire(&bcache.lock[hashid]);
  b->refcnt--;
  if (b->refcnt == 0) {
    // no one is waiting for it.
    b->next->prev = b->prev;
    b->prev->next = b->next;
    b->next = bcache.head[hashid].next;
    b->prev = &bcache.head[hashid];
    bcache.head[hashid].next->prev = b;
    bcache.head[hashid].next = b;
  }
  
  release(&bcache.lock[hashid]);
}

void
bpin(struct buf *b) {
  // xyf
  uint hashid = myhash(b->blockno);
  acquire(&bcache.lock[hashid]);
  b->refcnt++;
  release(&bcache.lock[hashid]);
}

void
bunpin(struct buf *b) {
  uint hashid = myhash(b->blockno);
  acquire(&bcache.lock[hashid]);
  b->refcnt--;
  release(&bcache.lock[hashid]);
}


