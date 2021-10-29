# Lab: locks

https://pdos.csail.mit.edu/6.S081/2020/labs/lock.html

<br>

这次实验中我们将重新设计 xv6 的**内存分配**和**磁盘块缓存**机制以提高它们的并行性。并行性的性能可以由锁争用的次数反应出来——差的并行代码会导致高锁争用。



## Memory allocator

任务：原本的 kalloc.c 导致高锁争用的原因是 xv6 只维护了一个空闲页面链表，该链表有一个锁。为了减少锁争用，我们可以给每一个 CPU 维护一个空闲页面链表，这样不同 CPU 就可以并行地内存分配和释放，因为它们之间相互独立。但是，当一个 CPU 的空闲页面被分配完之后，它需要从其他的 CPU 的空闲页面链表中**窃取**一部分空闲页面。窃取过程可能导致锁争用，但是不会很频繁。

我们的任务就是实现每一个 CPU 一个空闲链表，且在链表为空时窃取页面。所有锁的名字必须以 kmem 开头。kalloctest 检测是否减少了锁争用，usertests sbrkmuch 检测是否仍然能够分配所有内存。

首先把原来的一个 kmem 结构体改成一个数组 kmems，使每个 CPU 有一个对应的空闲页面链表：

```c
struct kmem{
  struct spinlock lock;
  struct run *freelist;
} kmems[NCPU];
```

然后每个 CPU 分别初始化：

```c
void
kinit()
{
  char lockname[6] = {0};
  for(int i = 0; i < NCPU; i++){
    snprintf(lockname, 5, "kmem%d", i);
    initlock(&kmems[i].lock, lockname);
    freerange(i == 0 ? end : (void*)PHYSTOP, (void*)PHYSTOP);
  }
  /*initlock(&kmem.lock, "kmem");
  freerange(end, (void*)PHYSTOP);*/
}
```

这里我偷了个懒，先给 0 号 CPU 分配所有内存，其他 CPU 不分配。由于一个 CPU 没有空闲页面时会去其他 CPU 处窃取，所以我希望足够长时间之后，页面能够大致平均分配开（这其实是一个数学问题……有时间可以研究一下……）。

kfree 把释放的页面放进**当前 CPU** 的空闲页面链表。

```c
void
kfree(void *pa)
{
  ...
  r = (struct run*)pa;
  push_off(); int ci = cpuid(); pop_off();
  acquire(&kmems[ci].lock);
  r->next = kmems[ci].freelist;
  kmems[ci].freelist = r;
  release(&kmems[ci].lock);
}
```

kalloc 从当前 CPU 空闲页面链表中取一个页面，如果不存在则窃取（steal）：

```c
void *
kalloc(void)
{
  struct run *r;
  ...
  push_off(); int ci = cpuid(); pop_off();
  acquire(&kmems[ci].lock);
  r = kmems[ci].freelist;
  if(r)
    kmems[ci].freelist = r->next;
  else // Steal from other CPU when freelist is empty.
    r = steal(ci);
  release(&kmems[ci].lock);
  ...
}
```

窃取我实现的很暴力，逐一检查各个 CPU 是否有空闲页面，有就窃取过来：

```c
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
```



## Buffer cache

xv6 在 bio.c 中实现了磁盘块的缓存机制，它是一个双向链表，每个元素是一个缓存块。一个缓存块（struct buf, kernel/buf.h）不仅包含数据，还包含有效位 valid、脏位 disk、设备号、磁盘块号、被引用次数等信息。**整个双向链表由一个自旋锁保护，每个缓存块都由一个睡眠锁保护。**

任务：由于整个缓存双向链表由一个自旋锁保护，所以多个进程反复读不同文件时会产生高锁争用。我们要更改缓存机制以减少锁争用。

我选择的方法是 Hash：原本只有一个双向链表维护所有缓存块，现在我开 NBUCKETS 个桶，每个桶里面维护一个双向链表。一个块应该去哪个桶里找由 Hash 确定。

我用的 Hash 函数长这样：

```c
#define NBUCKETS 13
inline uint myhash(uint blockno){
  uint res = 0;
  for(int i = 31; i > 0; i--)
    res = (blockno >> i) + (res << 6) + (res << 16) - res;
  res %= NBUCKETS;
  return res;
}
```

为什么用这样的 Hash 函数呢？不为什么，因为我喜欢（逃～。

<br>

现在干正事，首先修改 binit 给每个桶初始化：

```c
void
binit(void)
{
  struct buf *b;
  for(int i = 0; i < NBUCKETS; i++){
    char lockname[7];
    snprintf(lockname, 6, "bcache%d", i);
    initlock(&bcache.lock[i], lockname);
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
```

和 kinit 一样我偷了个懒，先把所有缓存块给 0 号桶，因为当一个桶缓存块不够用时会去其他桶窃取。

<br>

接下来修改 bget。原本的 bget 在双向链表中**扫描两次**，第一次**从前往后**找是否命中，如果命中则返回命中的缓存块；如果没命中，则第二次**从后往前**找一块未被映射的缓存块（refcnt = 0），加上映射之后返回之。这样扫描实现了 LRU 机制。

为了修改，我们除了把上述双向链表改成当前桶的双向链表以外，还需处理一种情况：如果当前桶没有空闲的缓存块，那么去其他桶窃取一块。为了方便，我把「从某桶取出某未映射缓存」写作如下函数：

```c
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
```

然后在 bget 中先对当前桶执行 bfind，如果没找到则遍历其他桶执行 bfind，把返回的缓存块插入当前桶的双向链表。有一个坑是：如果 bfind 的是当前桶，小心不要重复上锁。

<br>

brelse、bpin、bunpin 都只需要把原来的锁换成当前桶的锁即可，这里不再赘述。

<br>

make grade 截图：

![](README_img/result.png)
