## 12. sbrk的实现

`sbrk`是一个用于进程减少或增长其内存的系统调用。这个系统调用由函数`growproc`实现(**_kernel/proc.c_**:239)。`growproc`根据`n`是正的还是负的调用`uvmalloc`或`uvmdealloc`。`uvmalloc`(**_kernel/vm.c_**:229)用`kalloc`分配物理内存，并用`mappages`将PTE添加到用户页表中。`uvmdealloc`调用`uvmunmap`(**_kernel/vm.c_**:174)，`uvmunmap`使用`walk`来查找对应的PTE，并使用`kfree`来释放PTE引用的物理内存。

```c
int

growproc(int n)

{

  uint64 sz;

  struct proc *p = myproc();

  

  sz = p->sz;

  if(n > 0){

    if(sz + n > TRAPFRAME) {

      return -1;

    }

    if((sz = uvmalloc(p->pagetable, sz, sz + n, PTE_W)) == 0) {

      return -1;

    }

  } else if(n < 0){

    sz = uvmdealloc(p->pagetable, sz, sz + n);

  }

  p->sz = sz;

  return 0;

}
```

uvmalloc负责分配新的内存：

```c
// Allocate PTEs and physical memory to grow a process from oldsz to

// newsz, which need not be page aligned.  Returns new size or 0 on error.

uint64

uvmalloc(pagetable_t pagetable, uint64 oldsz, uint64 newsz, int xperm)

{

  char *mem;

  uint64 a;

  

  if(newsz < oldsz)

    return oldsz;

  

  oldsz = PGROUNDUP(oldsz);

  for(a = oldsz; a < newsz; a += PGSIZE){

    mem = kalloc();

    if(mem == 0){

      uvmdealloc(pagetable, a, oldsz);

      return 0;

    }
    // 让其未被分配，pte_v=0
    memset(mem, 0, PGSIZE);

    if(mappages(pagetable, a, PGSIZE, (uint64)mem, PTE_R|PTE_U|xperm) != 0){

      kfree(mem);

      uvmdealloc(pagetable, a, oldsz);

      return 0;

    }

  }

  return newsz;

}
```

uvmdealloc负责释放内存：

```c
// Deallocate user pages to bring the process size from oldsz to

// newsz.  oldsz and newsz need not be page-aligned, nor does newsz

// need to be less than oldsz.  oldsz can be larger than the actual

// process size.  Returns the new process size.

uint64

uvmdealloc(pagetable_t pagetable, uint64 oldsz, uint64 newsz)

{

  if(newsz >= oldsz)

    return oldsz;

  
  // 只有超出单页大小才删除分配
  if(PGROUNDUP(newsz) < PGROUNDUP(oldsz)){

    int npages = (PGROUNDUP(oldsz) - PGROUNDUP(newsz)) / PGSIZE;

    uvmunmap(pagetable, PGROUNDUP(newsz), npages, 1);

  }

  

  return newsz;

}
```

XV6使用进程的页表，不仅是告诉硬件如何映射用户虚拟地址，也是明晰哪一个物理页面已经被分配给该进程的唯一记录。这就是为什么释放用户内存（在`uvmunmap`中）需要检查用户页表的原因。

