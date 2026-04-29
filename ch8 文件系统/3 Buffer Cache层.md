## 4. Buffer Cache层的任务

Buffer cache有两个任务：

1. 同步对磁盘块的访问，以确保磁盘块在内存中只有一个副本，并且一次只有一个内核线程使用该副本
2. 缓存常用块，以便不需要从慢速磁盘重新读取它们。代码在***bio.c***中。

## 5. Buffer Cache层的接口

Buffer cache层导出的主接口主要是`bread`和`bwrite`；前者获取一个*buf*，其中包含一个可以在内存中读取或修改的块的副本，后者将修改后的缓冲区写入磁盘上的相应块。内核线程必须通过调用`brelse`释放缓冲区。

Buffer cache每个缓冲区使用一个睡眠锁，以确保每个缓冲区（因此也是每个磁盘块）每次只被一个线程使用；`bread`返回一个上锁的缓冲区，`brelse`释放该锁。

让我们回到Buffer cache。Buffer cache中保存磁盘块的缓冲区数量固定，这意味着如果文件系统请求还未存放在缓存中的块，Buffer cache必须回收当前保存其他块内容的缓冲区。Buffer cache为新块回收最近使用最少的缓冲区。这样做的原因是认为最近使用最少的缓冲区是最不可能近期再次使用的缓冲区。

## 6. Buffer Cache层的代码实现

Buffer cache是以**双链表**表示的缓冲区。

`main`（**_kernel/main.c_**:27）调用的函数`binit`使用静态数组`buf`（**_kernel/bio.c_**:43-52）中的`NBUF`个缓冲区初始化列表。对Buffer cache的所有其他访问都通过`bcache.head`引用链表，而不是`buf`数组。

```c
void

binit(void)

{

  struct buf *b;
  
  initlock(&bcache.lock, "bcache");

  // Create linked list of buffers

  bcache.head.prev = &bcache.head;

  bcache.head.next = &bcache.head;

  for(b = bcache.buf; b < bcache.buf + NBUF; b++) {

    b->next = bcache.head.next;

    b->prev = &bcache.head;

    initsleeplock(&b->lock, "buffer");

    bcache.head.next->prev = b;

    bcache.head.next       = b;

  }

}
```

缓冲区有两个与之关联的状态字段。字段`valid`表示缓冲区是否包含块的副本。字段`disk`表示缓冲区内容是否已交给磁盘，这可能会更改缓冲区（例如，将数据从磁盘写入`data`）。

```c
struct buf {

  int              valid; // has data been read from disk?

  int              disk;  // does disk "own" buf?

  uint             dev;

  uint             blockno;

  struct sleeplock lock;

  uint             refcnt;

  struct buf      *prev; // LRU cache list

  struct buf      *next;

  uchar            data[BSIZE];

};
```

`Bread`（**_kernel/bio.c_**:93）调用`bget`为给定扇区（**_kernel/bio.c_**:97）获取缓冲区。如果缓冲区需要从磁盘进行读取，`bread`会在返回缓冲区之前调用`virtio_disk_rw`来执行此操作。

```c
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
```

`Bget`（**_kernel/bio.c_**:59）扫描缓冲区列表，查找具有给定设备和扇区号（**_kernel/bio.c_**:65-73）的缓冲区。如果存在这样的缓冲区，`bget`将获取缓冲区的睡眠锁。然后`Bget`返回锁定的缓冲区。

```c
// Look through buffer cache for block on device dev.

// If not found, allocate a buffer.

// In either case, return locked buffer.

static struct buf *

bget(uint dev, uint blockno)

{

  struct buf *b;

  acquire(&bcache.lock);

  // Is the block already cached?

  for(b = bcache.head.next; b != &bcache.head; b = b->next) {

    if(b->dev == dev && b->blockno == blockno) {

      b->refcnt++;

      release(&bcache.lock);

      acquiresleep(&b->lock);

      return b;

    }

  }

  // Not cached.

  // Recycle the least recently used (LRU) unused buffer.

  for(b = bcache.head.prev; b != &bcache.head; b = b->prev) {

    if(b->refcnt == 0) {

      b->dev     = dev;

      b->blockno = blockno;

      b->valid   = 0;

      b->refcnt  = 1;

      release(&bcache.lock);

      acquiresleep(&b->lock);

      return b;

    }

  }

  panic("bget: no buffers");

}
```

如果对于给定的扇区没有缓冲区，`bget`必须创建一个，这可能会重用包含其他扇区的缓冲区。它再次扫描缓冲区列表，查找未在使用中的缓冲区（`b->refcnt = 0`）：任何这样的缓冲区都可以使用。`Bget`编辑缓冲区元数据以记录新设备和扇区号，并获取其睡眠锁。注意，`b->valid = 0`的布置确保了`bread`将从磁盘读取块数据，而不是错误地使用缓冲区以前的内容。

每个磁盘扇区最多有一个缓存缓冲区是非常重要的，并且因为文件系统使用缓冲区上的锁进行同步，可以确保读者看到写操作。`Bget`的从第一个检查块是否缓存的循环到第二个声明块现在已缓存（通过设置`dev`、`blockno`和`refcnt`）的循环，一直持有`bcache.lock`来确保此不变量。这会导致检查块是否存在以及（如果不存在）指定一个缓冲区来存储块具有原子性。

`bget`在`bcache.lock`临界区域之外获取缓冲区的睡眠锁是安全的，因为非零`b->refcnt`防止缓冲区被重新用于不同的磁盘块。睡眠锁保护块缓冲内容的读写，而`bcache.lock`保护有关缓存哪些块的信息。

如果所有缓冲区都处于忙碌，那么太多进程同时执行文件系统调用；`bget`将会`panic`。一个更优雅的响应可能是在缓冲区空闲之前休眠，尽管这样可能会出现死锁。

一旦`bread`读取了磁盘（如果需要）并将缓冲区返回给其调用者，调用者就可以独占使用缓冲区，并可以读取或写入数据字节。如果调用者确实修改了缓冲区，则必须在释放缓冲区之前调用`bwrite`将更改的数据写入磁盘。`Bwrite`（**_kernel/bio.c_**:107）调用`virtio_disk_rw`与磁盘硬件对话。

```c
// Write b's contents to disk.  Must be locked.

void

bwrite(struct buf *b)

{

  if(!holdingsleep(&b->lock))

    panic("bwrite");

  virtio_disk_rw(b, 1);

}
```

当调用方使用完缓冲区后，它必须调用`brelse`来释放缓冲区(`brelse`是`b-release`的缩写，这个名字很隐晦，但值得学习：它起源于Unix，也用于BSD、Linux和Solaris）。`brelse`（**_kernel/bio.c_**:117）释放睡眠锁并将缓冲区移动到链表的前面（**_kernel/bio.c_**:128-133）。移动缓冲区会使列表按缓冲区的使用频率排序（意思是释放）：列表中的第一个缓冲区是最近使用的，最后一个是最近使用最少的。`bget`中的两个循环利用了这一点：在最坏的情况下，对现有缓冲区的扫描必须处理整个列表，但首先检查最新使用的缓冲区（从`bcache.head`开始，然后是下一个指针），在引用局部性良好的情况下将减少扫描时间。选择要重用的缓冲区时，通过自后向前扫描（跟随`prev`指针）选择最近使用最少的缓冲区。

```c
// Release a locked buffer.

// Move to the head of the most-recently-used list.

void

brelse(struct buf *b)

{

  if(!holdingsleep(&b->lock))

    panic("brelse");

  releasesleep(&b->lock);

  acquire(&bcache.lock);

  b->refcnt--;

  if(b->refcnt == 0) {

    // no one is waiting for it.

    b->next->prev          = b->prev;

    b->prev->next          = b->next;

    b->next                = bcache.head.next;

    b->prev                = &bcache.head;

    bcache.head.next->prev = b;

    bcache.head.next       = b;

  }

  release(&bcache.lock);

}
```