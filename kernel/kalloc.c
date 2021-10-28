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

// xyf
struct kmem{
  struct spinlock lock;
  struct run *freelist;
} kmems[NCPU];

void
kinit()
{
  // xyf
  char lockname[6] = {0};
  for(int i = 0; i < NCPU; i++){
    snprintf(lockname, 5, "kmem%d", i);
    initlock(&kmems[i].lock, lockname);
    freerange(i == 0 ? end : (void*)PHYSTOP, (void*)PHYSTOP);
  }
  /*initlock(&kmem.lock, "kmem");
  freerange(end, (void*)PHYSTOP);*/
}

void
freerange(void *pa_start, void *pa_end)
{
  char *p;
  p = (char*)PGROUNDUP((uint64)pa_start);
  for(; p + PGSIZE <= (char*)pa_end; p += PGSIZE)
    kfree(p);
}

// Free the page of physical memory pointed at by v,
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

  // xyf
  push_off(); int ci = cpuid(); pop_off();
  acquire(&kmems[ci].lock);
  r->next = kmems[ci].freelist;
  kmems[ci].freelist = r;
  release(&kmems[ci].lock);
}

struct run *
steal(int ci)
{
  struct run *r = 0;
  for(int i = 0; i < NCPU; i++){
    if(i == ci) continue;
    acquire(&kmems[i].lock);
    if((r = kmems[i].freelist)){
      kmems[i].freelist = r->next;
      release(&kmems[i].lock);
      break;
    }
    release(&kmems[i].lock);
  }
  return r;
}

// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.
void *
kalloc(void)
{
  struct run *r;

  // xyf
  push_off(); int ci = cpuid(); pop_off();
  acquire(&kmems[ci].lock);
  r = kmems[ci].freelist;
  if(r)
    kmems[ci].freelist = r->next;
  else // Steal from other CPU when freelist is empty.
    r = steal(ci);
  release(&kmems[ci].lock);

  if(r)
    memset((char*)r, 5, PGSIZE); // fill with junk
  return (void*)r;
}
