## 19. 磁盘上的inode

磁盘上的inode结构体`struct dinode`包含一个`size`和一个块号数组（见图8.3）。
![](assets/dinode.png)

inode数据可以在`dinode`的`addrs`数组列出的块中找到。前面的`NDIRECT`个数据块被列在数组中的前`NDIRECT`个元素中；这些块称为直接块（direct blocks）。接下来的`NINDIRECT`个数据块不在inode中列出，而是在称为间接块（indirect block）的数据块中列出。`addrs`数组中的最后一个元素给出了间接块的地址。

因此，可以从inode中列出的块加载文件的前12 kB（`NDIRECT x BSIZE`）字节，而只有在查阅间接块后才能加载下一个256 kB（`NINDIRECT x BSIZE`）字节。

这是一个很好的磁盘表示，但对于客户端来说较复杂。函数`bmap`管理这种表示，以便实现我们将很快看到的如`readi`和`writei`这样的更高级例程。`bmap(struct inode *ip, uint bn)`返回索引结点`ip`的第`bn`个数据块的磁盘块号。如果`ip`还没有这样的块，`bmap`会分配一个。

```c
// On-disk inode structure

struct dinode {

  short type;               // File type

  short major;              // Major device number (T_DEVICE only)

  short minor;              // Minor device number (T_DEVICE only)

  short nlink;              // Number of links to inode in file system

  uint  size;               // Size of file (bytes)

  uint  addrs[NDIRECT + 1]; // Data block addresses

};
```

## 20. 函数bmap的实现

函数`bmap`（**_kernel/fs.c_**:378）从简单的情况开始：前面的`NDIRECT`个块在inode本身中列出（**_kernel/fs.c_**:383-387）中。下面`NINDIRECT`个块在`ip->addrs[NDIRECT]`的间接块中列出。`Bmap`读取间接块（**_kernel/fs.c_**:394），然后从块内的正确位置（**_kernel/fs.c_**:395）读取块号。如果块号超过`NDIRECT+NINDIRECT`，则`bmap`调用`panic`崩溃；`writei`包含防止这种情况发生的检查（**_kernel/fs.c_**:490）。

```c
// Inode content

//

// The content (data) associated with each inode is stored

// in blocks on the disk. The first NDIRECT block numbers

// are listed in ip->addrs[].  The next NINDIRECT blocks are

// listed in block ip->addrs[NDIRECT].

// Return the disk block address of the nth block in inode ip.

// If there is no such block, bmap allocates one.

static uint

bmap(struct inode *ip, uint bn)

{

  uint        addr, *a;

  struct buf *bp;

  

  if(bn < NDIRECT) {

    if((addr = ip->addrs[bn]) == 0)

      ip->addrs[bn] = addr = balloc(ip->dev);

    return addr;

  }

  bn -= NDIRECT;

  

  if(bn < NINDIRECT) {

    // Load indirect block, allocating if necessary.

    if((addr = ip->addrs[NDIRECT]) == 0)

      ip->addrs[NDIRECT] = addr = balloc(ip->dev);

    bp = bread(ip->dev, addr);

    a  = (uint *)bp->data;

    if((addr = a[bn]) == 0) {

      a[bn] = addr = balloc(ip->dev);

      log_write(bp);

    }

    brelse(bp);

    return addr;

  }

  

  panic("bmap: out of range");

}
```

`Bmap`根据需要分配块。`ip->addrs[]`或间接块中条目为零表示未分配块。当`bmap`遇到零时，它会用按需分配的新块（**_kernel/fs.c_**:384-385）（**_kernel/fs.c_**:392-393）替换它们。

`itrunc`释放文件的块，将inode的`size`重置为零。`Itrunc`（**_kernel/fs.c_**:410）首先释放直接块（**_kernel/fs.c_**:416-421），然后释放间接块中列出的块（**_kernel/fs.c_**:426-429），最后释放间接块本身（**_kernel/fs.c_**:431-432）。

```c
// Truncate inode (discard contents).

// Caller must hold ip->lock.

void

itrunc(struct inode *ip)

{

  int         i, j;

  struct buf *bp;

  uint       *a;

  

  for(i = 0; i < NDIRECT; i++) {

    if(ip->addrs[i]) {

      bfree(ip->dev, ip->addrs[i]);

      ip->addrs[i] = 0;

    }

  }

  

  if(ip->addrs[NDIRECT]) {

    bp = bread(ip->dev, ip->addrs[NDIRECT]);

    a  = (uint *)bp->data;

    for(j = 0; j < NINDIRECT; j++) {

      if(a[j])

        bfree(ip->dev, a[j]);

    }

    brelse(bp);

    bfree(ip->dev, ip->addrs[NDIRECT]);

    ip->addrs[NDIRECT] = 0;

  }

  

  ip->size = 0;

  iupdate(ip);

}
```

`Bmap`使`readi`和`writei`很容易获取inode的数据。`Readi`（**_kernel/fs.c_**:456）首先确保偏移量和计数不超过文件的末尾。开始于超过文件末尾的地方读取将返回错误（**_kernel/fs.c_**:461-462），而从文件末尾开始或穿过文件末尾的读取返回的字节数少于请求的字节数（**_kernel/fs.c_**:463-464）。主循环处理文件的每个块，将数据从缓冲区复制到`dst`（**_kernel/fs.c_**:466-474）。

```c
// Read data from inode.

// Caller must hold ip->lock.

// If user_dst==1, then dst is a user virtual address;

// otherwise, dst is a kernel address.

int

readi(struct inode *ip, int user_dst, uint64 dst, uint off, uint n)

{

  uint        tot, m;

  struct buf *bp;

  

  if(off > ip->size || off + n < off)

    return 0;

  if(off + n > ip->size)

    n = ip->size - off;

  

  for(tot = 0; tot < n; tot += m, off += m, dst += m) {

    bp = bread(ip->dev, bmap(ip, off / BSIZE));

    m  = min(n - tot, BSIZE - off % BSIZE);

    if(either_copyout(user_dst, dst, bp->data + (off % BSIZE), m) == -1) {

      brelse(bp);

      tot = -1;

      break;

    }

    brelse(bp);

  }

  return tot;

}
```

`writei`（**_kernel/fs.c_**:483）与`readi`相同，但有三个例外：从文件末尾开始或穿过文件末尾的写操作会使文件增长到最大文件大小（**_kernel/fs.c_**:490-491）；循环将数据复制到缓冲区而不是输出（kernel/fs.c:36）；如果写入扩展了文件，`writei`必须更新其大小（**_kernel/fs.c_**:504-511）。

```c
// Write data to inode.

// Caller must hold ip->lock.

// If user_src==1, then src is a user virtual address;

// otherwise, src is a kernel address.

// Returns the number of bytes successfully written.

// If the return value is less than the requested n,

// there was an error of some kind.

int

writei(struct inode *ip, int user_src, uint64 src, uint off, uint n)

{

  uint        tot, m;

  struct buf *bp;

  if(off > ip->size || off + n < off)

    return -1;

  if(off + n > MAXFILE * BSIZE)

    return -1;

  for(tot = 0; tot < n; tot += m, off += m, src += m) {

    bp = bread(ip->dev, bmap(ip, off / BSIZE));

    m  = min(n - tot, BSIZE - off % BSIZE);

    if(either_copyin(bp->data + (off % BSIZE), user_src, src, m) == -1) {

      brelse(bp);

      break;

    }

    log_write(bp);

    brelse(bp);

  } 

  if(off > ip->size)

    ip->size = off;

  // write the i-node back to disk even if the size didn't change

  // because the loop above might have called bmap() and added a new

  // block to ip->addrs[].

  iupdate(ip);

  return tot;

}
```

`readi`和`writei`都是从检查`ip->type == T_DEV`开始的。这种情况处理的是数据不在文件系统中的特殊设备；我们将在文件描述符层返回到这种情况。

函数`stati`（**_kernel/fs.c_**:442）将inode元数据复制到`stat`结构体中，该结构通过`stat`系统调用向用户程序公开。