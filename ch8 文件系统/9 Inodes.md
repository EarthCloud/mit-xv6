## 16. 分配inode的过程

为了分配新的inode（例如，在创建文件时），xv6调用`ialloc`（**_kernel/fs.c_**:196）。

```c
// Allocate an inode on device dev.

// Mark it as allocated by  giving it type type.

// Returns an unlocked but allocated and referenced inode.

struct inode *

ialloc(uint dev, short type)

{

  int            inum;

  struct buf    *bp;

  struct dinode *dip;

  

  for(inum = 1; inum < sb.ninodes; inum++) {

    bp  = bread(dev, IBLOCK(inum, sb));

    dip = (struct dinode *)bp->data + inum % IPB;

    if(dip->type == 0) { // a free inode

      memset(dip, 0, sizeof(*dip));

      dip->type = type;

      log_write(bp); // mark it allocated on the disk

      brelse(bp);

      return iget(dev, inum);

    }

    brelse(bp);

  }

  panic("ialloc: no inodes");

}
```

`Ialloc`类似于`balloc`：它一次一个块地遍历磁盘上的索引节点结构体，查找标记为空闲的一个。当它找到一个时，它通过将新`type`写入磁盘来声明它，然后末尾通过调用`iget`（**_kernel/fs.c_**:210）从inode缓存返回一个条目。

`ialloc`的正确操作取决于这样一个事实：一次只有**一个进程**可以保存对`bp`的引用：`ialloc`可以确保其他进程不会同时看到inode可用并尝试声明它。

`Iget`（**_kernel/fs.c_**:243）在inode缓存中查找具有所需设备和inode编号的活动条目（`ip->ref > 0`）。如果找到一个，它将返回对该incode的新引用（**_kernel/fs.c_**:252-256）。在`iget`扫描时，它会记录第一个空槽（**_kernel/fs.c_**:257-258）的位置，如果需要分配缓存项，它会使用这个槽。

```c
// Find the inode with number inum on device dev

// and return the in-memory copy. Does not lock

// the inode and does not read it from disk.

static struct inode *

iget(uint dev, uint inum)

{

  struct inode *ip, *empty;
  acquire(&icache.lock);
  // Is the inode already cached?

  empty = 0;

  for(ip = &icache.inode[0]; ip < &icache.inode[NINODE]; ip++) {

    if(ip->ref > 0 && ip->dev == dev && ip->inum == inum) {

      ip->ref++;

      release(&icache.lock);

      return ip;

    }

    if(empty == 0 && ip->ref == 0) // Remember empty slot.

      empty = ip;

  }
  // Recycle an inode cache entry.

  if(empty == 0)

    panic("iget: no inodes");

  ip        = empty;

  ip->dev   = dev;

  ip->inum  = inum;

  ip->ref   = 1;

  ip->valid = 0;

  release(&icache.lock);

  return ip;

}
```

在读取或写入inode的元数据或内容之前，代码必须使用`ilock`锁定inode。`Ilock`（kernel/fs.c:289）为此使用睡眠锁。一旦`ilock`以独占方式访问inode，它将根据需要从磁盘（更可能是buffer cache）读取inode。函数`iunlock`（**_kernel/fs.c_**:317）释放睡眠锁，这可能会导致任何睡眠进程被唤醒。

`Iput`（**_kernel/fs.c_**:333）通过减少引用计数（**_kernel/fs.c_**:356）释放指向inode的C指针。如果这是最后一次引用，inode缓存中该inode的槽现在将是空闲的，可以重用于其他inode。

```c
// Drop a reference to an in-memory inode.
// If that was the last reference, the inode cache entry can
// be recycled.
// If that was the last reference and the inode has no links
// to it, free the inode (and its content) on disk.
// All calls to iput() must be inside a transaction in
// case it has to free the inode.

void

iput(struct inode *ip)

{

  acquire(&icache.lock);

  if(ip->ref == 1 && ip->valid && ip->nlink == 0) {

    // inode has no links and no other references: truncate and free.

    // ip->ref == 1 means no other process can have ip locked,

    // so this acquiresleep() won't block (or deadlock).

    acquiresleep(&ip->lock);

    release(&icache.lock);

    itrunc(ip);

    ip->type = 0;

    iupdate(ip);

    ip->valid = 0;

    releasesleep(&ip->lock);

    acquire(&icache.lock);

  }

  ip->ref--;

  release(&icache.lock);

}
```

如果`iput`发现没有指向inode的C指针引用，并且inode没有指向它的链接（发生于无目录），则必须释放inode及其数据块。`Iput`调用`itrunc`将文件截断为零字节，释放数据块；将索引节点类型设置为0（未分配）；并将inode写入磁盘（**_kernel/fs.c_**:338）。

## 17. iput中的锁机制

`iput`中释放inode的锁定协议值得仔细研究。

一个危险是并发线程可能正在`ilock`中等待使用该inode（例如，读取文件或列出目录），并且不会做好该inode已不再被分配的准备。这不可能发生，因为如果缓存的inode没有链接，并且`ip->ref`为1，那么系统调用就无法获取指向该inode的指针。那一个引用是调用`iput`的线程所拥有的引用。的确，`iput`在`icache.lock`的临界区域之外检查引用计数是否为1，但此时已知链接计数为零，因此没有线程会尝试获取新引用。

另一个主要危险是，对`ialloc`的并发调用可能会选择`iput`正在释放的同一个inode。这只能在`iupdate`写入磁盘以使inode的`type`为零后发生。这个争用是良性的：分配线程将客气地等待获取inode的睡眠锁，然后再读取或写入inode，此时`iput`已完成。

`iput()`可以写入磁盘。这意味着任何使用文件系统的系统调用都可能写入磁盘，因为系统调用可能是最后一个引用该文件的系统调用。即使像`read()`这样看起来是只读的调用，也可能最终调用`iput()`。这反过来意味着，即使是只读系统调用，如果它们使用文件系统，也必须在**事务**中进行包装。

## 18. iput和崩溃机制

`iput()`和崩溃之间存在一种具有挑战性的交互。`iput()`不会在文件的链接计数降至零时立即截断文件，因为某些进程可能仍在内存中保留对inode的引用：进程可能仍在读取和写入该文件，因为它已成功打开该文件。但是，如果在最后一个进程关闭该文件的文件描述符之前发生崩溃，则该文件将被标记为已在磁盘上分配，但没有目录项指向它。

文件系统以两种方式之一处理这种情况。简单的解决方案用于恢复时：重新启动后，文件系统会扫描整个文件系统，以查找标记为已分配但没有指向它们的目录项的文件。如果存在任何此类文件，接下来可以将其释放。

第二种解决方案不需要扫描文件系统。在此解决方案中，文件系统在磁盘（例如在超级块中）上记录链接计数降至零但引用计数不为零的文件的i-number。如果文件系统在其引用计数达到0时删除该文件，则会通过从列表中删除该inode来更新磁盘列表。恢复时，文件系统将释放列表中的任何文件。

Xv6**没有实现**这两种解决方案，这意味着inode可能被标记为已在磁盘上分配，即使它们不再使用。这意味着随着时间的推移，xv6可能会面临磁盘空间不足的风险。