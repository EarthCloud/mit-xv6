## 1. cow的基本思想：

在课堂上，我们知道，COW fork把父子进程需要的页面映射到同一物理页上并标为只读，当父进程或子进程实际需要写入内容时，触发Page Fault，再在此时复制出新的页面来，修改映射到此处。

这个想法听起来很简单，但在实际进行实现的时候需要面对诸多问题，因此，我们由简单到复杂，先把框架搭好，再进行深入。

## 2. 我们都需要什么：

### a. 引用计数

由于现在，一个物理页面可能被多个虚拟地址引用，我们必须添加引用计数，并只在引用计数为0的时候才真正释放页面。这样，即使是一个cow页面，它对内核的其他模块所作的kfree等操作也是透明的，我们可以隐藏掉cow实现的细节。

由于修改引用计数必须是一个原子操作，我们需要申请一把自旋锁。

```c
// ref count array
struct {
  struct spinlock lock;
  int             count[PHYSTOP / PGSIZE];
} ref;
```

简单计算，增加引用计数数组的开销很小，可以接受。

记得在刚开始初始化的时候，内核把脏页调用kfree添加到了freelist链表中，所以我们应该在初始化前先将引用计数设为1。

先是一个单独的函数封装：

```c
void
kaddref(void *pa)
{
  if(((uint64)pa % PGSIZE) != 0 || (char *)pa < end || (uint64)pa >= PHYSTOP)
    panic("kaddref: invalid address");

  int idx = (uint64)pa / PGSIZE;
  // add must be locked
  acquire(&ref.lock);
  ref.count[idx]++;
  release(&ref.lock);
}
```

初始化锁：

```c
void
kinit()
{
  initlock(&kmem.lock, "kmem");
  initlock(&ref.lock, "ref_count");
  freerange(end, (void *)PHYSTOP);
}
```

然后在freerange中添加计数：

```c
void
freerange(void *pa_start, void *pa_end)
{
  char *p;
  p = (char *)PGROUNDUP((uint64)pa_start);
  for(; p + PGSIZE <= (char *)pa_end; p += PGSIZE) {
    // add a count to keep kfree init safe
    kaddref(p);
    kfree(p);
  }
}
```

kfree修改的逻辑较多：

```c
// Free the page of physical memory pointed at by v,
// which normally should have been returned by a
// call to kalloc().  (The exception is when
// initializing the allocator; see kinit above.)
void
kfree(void *pa)
{
  struct run *r;

  if(((uint64)pa % PGSIZE) != 0 || (char *)pa < end || (uint64)pa >= PHYSTOP)
    panic("kfree: invalid address");

  acquire(&ref.lock);
  if(ref.count[(uint64)pa / PGSIZE] <= 0) {
    panic("kfree: invalid free");
  }
  int current_count = --ref.count[(uint64)pa / PGSIZE];
  release(&ref.lock);
  if(current_count > 0) {
    return;
  }
  // Fill with junk to catch dangling refs.
  memset(pa, 1, PGSIZE);

  r = (struct run *)pa;

  acquire(&kmem.lock);
  r->next       = kmem.freelist;
  kmem.freelist = r;
  release(&kmem.lock);
}
```

在kalloc中记得增加引用计数：

```c
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
    memset((char *)r, 5, PGSIZE); // fill with junk
    kaddref(r);
  }
  return (void *)r;
}
```

### b. PTE_C标志位

我们需要将COW页面添加一个标志位，否则，内核不知道这个Page Fault究竟是不是由COW页面引起的。

```c
#define PTE_C (1L << 8) // Copy-on-write PTE
```

找个保留的位，第九第十位都行。

## 3. 正式开始实现

我们首先要做的，就是修改支撑fork调用的kvmcopy函数，在这里不需要调用kalloc了，增加引用计数并修改最后一级PTE的标志位就行了，同时，对子进程，进行映射：

```c
// Given a parent process's page table, copy
// its memory into a child's page table.
// Copies both the page table and the
// physical memory.
// returns 0 on success, -1 on failure.
// frees any allocated pages on failure.
int
uvmcopy(pagetable_t old, pagetable_t new, uint64 sz)
{
  pte_t *pte;
  uint64 pa, i;
  uint   flags;

  for(i = 0; i < sz; i += PGSIZE) {
    if((pte = walk(old, i, 0)) == 0)
      panic("uvmcopy: pte should exist");
    if((*pte & PTE_V) == 0)
      panic("uvmcopy: page not present");
    pa    = PTE2PA(*pte);
    flags = PTE_FLAGS(*pte);
    // Set new flags of each
    if(flags & PTE_W) {
      flags = (flags & ~PTE_W) | PTE_C;
    }
    // Set new ref count
    kaddref((void *)pa);
    // Modify parent pte
    *pte = PA2PTE(pa) | flags | PTE_V;
    // Map child pte
    if(mappages(new, i, PGSIZE, pa, flags) != 0) {
      kfree((void *)pa);
      goto err;
    }
  }
  // Refresh TLB since we modified the permissions
  sfence_vma();
  return 0;

err:
  uvmunmap(new, 0, i / PGSIZE, 1);
  return -1;
}
```

我们尽量把处理cow page fault的逻辑封装一下，要不然你后来会发现，用到这个逻辑的地方不止一处。

```c
// When write to cow page, use this to handle
// Return 0 when success
// -1 on failure
int
cow_fault_handler(uint64 va)
{
  pte_t       *pte;
  uint64       pa;
  uint         new_flags;
  char        *mem;
  struct proc *p = myproc();

  if(va >= p->sz)
    return -1;
  va = PGROUNDDOWN(va);
  // check if the pte is valid and cow page
  if((pte = walk(p->pagetable, va, 0)) == 0)
    return -1;
  if((*pte & PTE_V) == 0)
    return -1;
  if((*pte & PTE_C) == 0)
    return -1;

  pa        = PTE2PA(*pte);
  new_flags = (PTE_FLAGS(*pte) & (~PTE_C)) | PTE_W;
  if((mem = kalloc()) == 0)
    return -1;

  memmove(mem, (char *)pa, PGSIZE);
  *pte = PA2PTE((uint64)mem) | new_flags;

  kfree((void *)pa);
  sfence_vma();
  return 0;
}
```

它只是简单检查了地址的合法性，然后把cow page中的内容复制到新的页面上。

然后就可以在trap.c中先调用它了：

```c
  } else if(r_scause() == 15) {
    if(cow_fault_handler(r_stval()) != 0) {
      p->killed=1;
    }
  } else if((which_dev = devintr()) != 0) {
    // ok
```

## 4. 怎么过不了测试捏

这些修改不足以支撑我们通过测试。经过排查，你会发现错误很可能是copyout这个函数。lab的提示里很贴心的为我们也指出了。它的问题在哪呢，我们先来简单看一下copyout的源码。

```c
int
copyout(pagetable_t pagetable, uint64 dstva, char *src, uint64 len)
{
  uint64 n, va0, pa0;

  while(len > 0) {
    va0 = PGROUNDDOWN(dstva);
    pa0 = walkaddr(pagetable, va0);
    if(pa0 == 0)
      return -1;
    n = PGSIZE - (dstva - va0);
    if(n > len)
      n = len;
    memmove((void *)(pa0 + (dstva - va0)), src, n);

    len -= n;
    src += n;
    dstva = va0 + PGSIZE;
  }
  return 0;
}
```

回想一下 Lazy Allocation 实验，当内核执行系统调用（比如 `read`，把文件数据读到用户给的缓冲区里）时，它会调用 `kernel/vm.c` 里的 `copyout` 函数。

`copyout` 的核心逻辑是这样的：

1. 它拿到了用户传进来的虚拟地址 `dstva`。
2. 它调用 `walkaddr` 翻译出对应的物理地址 `pa0`。
3. 它直接在内核态调用 `memmove(pa0, src, n)` 把数据写进去。

假设用户传进来的是一个 COW 页面（带着 `PTE_C`，没有 `PTE_W` 写权限）。 如果这段代码在用户态执行，CPU 会立刻拦截并触发 15 号中断。但是！`copyout` 是在内核态执行的！ 通过 `walkaddr` 拿到物理地址 `pa0` 后，内核是使用自己的“直接映射（Direct Map）”去访问这块物理内存的，这个映射拥有至高无上的读写权限。CPU 的 MMU 根本不会拦截内核的 `memmove`。

copyout的memmove直接通过解引用内核空间的地址来实现！我们的页面很有可能是cow页，对它写入显然是不合法的！

```c
// Copy from kernel to user.
// Copy len bytes from src to virtual address dstva in a given page table.
// Return 0 on success, -1 on error.
int
copyout(pagetable_t pagetable, uint64 dstva, char *src, uint64 len)
{
  uint64 n, va0, pa0;
  pte_t *pte;

  while(len > 0) {
    va0 = PGROUNDDOWN(dstva);
    // firstly check to avoid panic
    if (va0>=MAXVA) {
      return -1;
    }
    pte = walk(pagetable, va0, 0);
    if(pte != 0 && (*pte & PTE_V) && (*pte & PTE_U) && (*pte & PTE_C)) {
      if(cow_fault_handler(va0) != 0) {
        return -1;
      }
    }
    
    pa0 = walkaddr(pagetable, va0);
    if(pa0 == 0)
      return -1;
    n = PGSIZE - (dstva - va0);
    if(n > len)
      n = len;
    memmove((void *)(pa0 + (dstva - va0)), src, n);

    len -= n;
    src += n;
    dstva = va0 + PGSIZE;
  }
  return 0;
}
```

`copyin` 是内核从用户空间**读取**数据。读取 COW 共享页面完全是合法且安全的，不需要复制物理页。所以 `copyin` 保持原样即可。这也是为什么我们在 `copyout` 里拦截，而不是去改底层的 `walkaddr`（因为 `walkaddr` 被读写操作共同使用）。

## 5. 为什么还是过不了捏？

这是整个实验里最令人困扰的地方，如果不借助Gemini，我觉得我很难自己发现这个问题。

上面的修改结束后，当你执行cowtests，很可能会碰到以下报错：

```text
$ cowtests

exec cowtests failed
```

当我们输入 `$ cowtests` 并回车时，发生了一系列事情：

1. `sh` 读取了输入，并调用了 `fork()`。此时父子进程共享内存，栈变成了 COW 页面。
2. 子进程 `sh` 尝试执行 `exec("cowtests")`。
3. `exec` 是一个极其特殊的系统调用：它会为新程序**创建一个全新的页表（New Pagetable）**，分配新的栈，然后使用 `copyout` 把命令行参数 `"cowtests"` 拷贝到这个**新栈**里。
4. 注意！在这个关键时刻，`copyout` 收到的 `pagetable` 参数是**新页表**，但当前进程 `myproc()->pagetable` 仍然是**老页表**！

对应到exec的代码中就是这样：

```c
  // Copy argument strings into new stack, remember their

  // addresses in ustack[].

  for(argc = 0; argv[argc]; argc++) {

    if(argc >= MAXARG)

      goto bad;

    sp -= strlen(argv[argc]) + 1;

    sp -= sp % 16; // riscv sp must be 16-byte aligned

    if(sp < stackbase)

      goto bad;

    if(copyout(pagetable, sp, argv[argc], strlen(argv[argc]) + 1) < 0)

      goto bad;

    ustack[argc] = sp;

  }

  ustack[argc] = 0;
```

问题就出在了这里的copyout。

所以，我们应该对cow page的handler进行更优化的封装，使其可以使用对应的页表：

```c
// When write to cow page, use this to handle

// Return 0 when success

// -1 on failure

int

cow_fault_handler(pagetable_t pagetable, uint64 va)

{

  pte_t *pte;

  uint64 pa;

  uint   new_flags;

  char  *mem;

  

  if(va >= MAXVA)

    return -1;

  va = PGROUNDDOWN(va);

  // check if the pte is valid and cow page

  if((pte = walk(pagetable, va, 0)) == 0)

    return -1;

  if((*pte & PTE_V) == 0)

    return -1;

  if((*pte & PTE_C) == 0)

    return -1;

  

  pa        = PTE2PA(*pte);

  new_flags = (PTE_FLAGS(*pte) & (~PTE_C)) | PTE_W;

  if((mem = kalloc()) == 0)

    return -1;

  

  memmove(mem, (char *)pa, PGSIZE);

  *pte = PA2PTE((uint64)mem) | new_flags;

  

  kfree((void *)pa);

  sfence_vma();

  return 0;

}
```

然后更新对cow_fault_handler的引用，我们就成功通过了usertests和cowtests。

```text
$ make qemu-gdb
(7.2s) 
== Test   simple == 
  simple: OK 
== Test   three == 
  three: OK 
== Test   file == 
  file: OK 
== Test usertests == 
$ make qemu-gdb
(45.8s) 
== Test   usertests: copyin == 
  usertests: copyin: OK 
== Test   usertests: copyout == 
  usertests: copyout: OK 
== Test   usertests: all tests == 
  usertests: all tests: OK 
== Test time == 
time: OK 
Score: 110/110
```
