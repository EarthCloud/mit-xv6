这个b实验我写了踏马得有三天，中间卡了很长时间，我将完整叙述这个沟槽的过程：

---

## 1. 翻译题干

后面两个实验的目的是让内核可以直接解引用用户态的虚拟指针。

因为用户的每个进程都有自己的页表，而进入到内核态时，我们使用的是一个叫做kernel_pagetable的内核页表，其中并没有用户空间的相关映射。我们在调用copyin时，需要先获取进程页表，使用walk得到物理地址。

这有两个问题：
1. walk是软件模拟的，而使用\*运算符解引用时，CPU的MMU硬件直接通过RISC-V Sv39约定的三级页表方式访问物理地址，效率较高。
2. 物理页是不连续的，如果用之前的方法，我们必须逐页拷贝。

同时，共享的内核页表kernel_pagetable无法保证隔离，这之中映射了所有的内核栈，如果某些恶意的进程在堆上分配很大的空间，甚至有可能穿刺保护页，进入到其他进程的栈空间。所以为了隔离性，我们需要把内核页表也隔离掉。

这个实验是第一步，要求我们实现每个进程有自己独立的内核页，从本个实验开始，我采用细粒度的Git提交，并在Message中详细写出自己的修改内容，格式如下：

```
[<Lab>] <Module>: <在这里简述任务>

1. <在这里详细叙述任务>
   ·   ·   ·
```

例如：

```
[a kpgtb per proc] vm, proc: fix related issue

1. vm: add return for kvmmake
    
2. proc: fix procinit call to kvmmap
```

在之后的叙述中，我将直接引用提交编码，你可以在本仓库中看到我的提交。

我们开始吧：

---

## 2. 添加内核页位置：

```c
  // these are private to the process, so p->lock need not be held.

  uint64 kstack;               // Virtual address of kernel stack

  uint64 sz;                   // Size of process memory (bytes)

  pagetable_t pagetable;       // User page table
  // HERE
  pagetable_t kpagetable;      // User kernel page table

  struct trapframe *trapframe; // data page for trampoline.S

  struct context context;      // swtch() here to run process

  struct file *ofile[NOFILE];  // Open files

  struct inode *cwd;           // Current directory

  char name[16];               // Process name (debugging)
```

在`struct proc`中添加内核的字段。

## 3. 使用页表构造方法

在提交#8b25448中，我创建了kvmmake函数并为kvmmap添加了一个页表的参数：

```c
pagetable_t kvmmake(){

  

  pagetable_t pgtbl = (pagetable_t)kalloc();

  memset(pgtbl, 0, PGSIZE);

  

  // uart registers

  kvmmap(pgtbl, UART0, UART0, PGSIZE, PTE_R | PTE_W);

  

  // virtio mmio disk interface

  kvmmap(pgtbl, VIRTIO0, VIRTIO0, PGSIZE, PTE_R | PTE_W);

  

  // CLINT

  kvmmap(pgtbl, CLINT, CLINT, 0x10000, PTE_R | PTE_W);

  

  // PLIC

  kvmmap(pgtbl, PLIC, PLIC, 0x400000, PTE_R | PTE_W);

  

  // map kernel text executable and read-only.

  kvmmap(pgtbl, KERNBASE, KERNBASE, (uint64)etext - KERNBASE, PTE_R | PTE_X);

  

  // map kernel data and the physical RAM we'll make use of.

  kvmmap(pgtbl, (uint64)etext, (uint64)etext, PHYSTOP - (uint64)etext, PTE_R | PTE_W);

  

  // map the trampoline for trap entry/exit to

  // the highest virtual address in the kernel.

  kvmmap(pgtbl, TRAMPOLINE, (uint64)trampoline, PGSIZE, PTE_R | PTE_X);
  
  return pgtbl;
}

  

/*

 * create a direct-map page table for the kernel.

 */

void kvminit()

{

  kernel_pagetable = kvmmake();

}
```

```c
// add a mapping to the kernel page table.

// only used when booting.

// does not flush TLB or enable paging.

void kvmmap(pagetable_t pagetable,uint64 va, uint64 pa, uint64 sz, int perm)

{

  if (mappages(pagetable, va, sz, pa, perm) != 0)

    panic("kvmmap");

}
```

这些硬件和内核的映射总是相同的，我们直接硬编码就好了。

在课上，Frans给的解法是对于高位的（即L0索引大于0的PTE），直接复制kernel_pagetable的L0 PTE即可，这样后两级对于所有页表都是共享的，这样节省了空间。我是直接全部复制了，实现是差不多的。
## 4. 误解1

我最初认为，所有进程的内核栈都要映射进任意进程的内核页表中，虽然这样应该也能通过usertests，但为了追求隔离性，好的实践应该是只映射该进程的内核栈。

提交#1e87e88和提交#eff465c记录了我创建一个`proc_mapkstacks`并最终被弃用的过程。

## 5. 修改allocproc

最初时，我们的内核栈在procinit时就全部创建好并映射进内核页表了：

```c
extern pagetable_t kernel_pagetable;

  

// initialize the proc table at boot time.

void procinit(void)

{

  struct proc *p;

  

  initlock(&pid_lock, "nextpid");

  for (p = proc; p < &proc[NPROC]; p++)

  {

    initlock(&p->lock, "proc");

  

    // Allocate a page for the process's kernel stack.

    // Map it high in memory, followed by an invalid

    // guard page.

    char *pa = kalloc();

    if (pa == 0)

      panic("kalloc");

    uint64 va = KSTACK((int)(p - proc));

    kvmmap(kernel_pagetable, va, (uint64)pa, PGSIZE, PTE_R | PTE_W);

    p->kstack = va;

  }

  kvminithart();

}
```

我们需要将这段逻辑迁移到allocproc，此时父进程真正开始创建内核进程：

```c
  // Allocate a page for the process's kernel stack.

  // Map it high in memory, followed by an invalid

  // guard page.

  p->kpagetable = kvmmake();

  if (p->kpagetable == 0)

  {

    freeproc(p);

    release(&p->lock);

    return 0;

  }

  char *pa = kalloc();

  if (pa == 0)

  {

    freeproc(p);

    release(&p->lock);

    return 0;

  }

  kvmmap(p->kpagetable, p->kstack, (uint64)pa, PGSIZE, PTE_R | PTE_W);
```

我们在此处创建了页表，并且分配了内核栈。

## 6. 设置调度器

我们需要在调度器（scheduler）中顺带切换satp寄存器，并刷新TLB，并在时间片用完后切换回kernel_pagetable。

```c
        // Switch to chosen process's kernel page table

        w_satp(MAKE_SATP(p->kpagetable));

        sfence_vma();

  

        swtch(&c->context, &p->context);

  

        // Process is done running for now.

        // It should have changed its p->state before coming back.

        c->proc = 0;

        kvminithart();
```
## 7. 修改kvmpa

kvmpa是一个相当重要的函数，它将内核的虚拟地址转换为物理地址，文件系统依赖于此函数，它最初默认内核页表为kernel_pagetable，我们需要它动态读取当前进程的内核页表，遂做出如下修改，在提交#252869b中：

```c
// translate a kernel virtual address to

// a physical address. only needed for

// addresses on the stack.

// assumes va is page aligned.

uint64

kvmpa(uint64 va)

{

  uint64 off = va % PGSIZE;

  pte_t *pte;

  uint64 pa;

  pagetable_t kpt = kernel_pagetable;

  

  struct proc *p = myproc();

  if(p!=0)

    kpt = p->kpagetable;

  pte = walk(kpt, va, 0);

  if (pte == 0)

    panic("kvmpa");

  if ((*pte & PTE_V) == 0)

    panic("kvmpa");

  pa = PTE2PA(*pte);

  return pa + off;

}
```

## 8. 如何释放页表？

我们有一个能借助walk来释放页表的函数freewalk，它递归地释放页表：

```c
// Recursively free page-table pages.

// All leaf mappings must already have been removed.

void freewalk(pagetable_t pagetable)

{

  // there are 2^9 = 512 PTEs in a page table.

  for (int i = 0; i < 512; i++)

  {

    pte_t pte = pagetable[i];

    if ((pte & PTE_V) && ((pte & (PTE_R | PTE_W | PTE_X)) == 0))

    {

      // this PTE points to a lower-level page table.

      uint64 child = PTE2PA(pte);

      freewalk((pagetable_t)child);

      pagetable[i] = 0;

    }

    else if (pte & PTE_V)

    {

      panic("freewalk: leaf");

    }

  }

  kfree((void *)pagetable);

}
```

但是，它假设我们把所有物理内存上存的内容都清除了。在本实验中，我们只是清除映射，实际的设备映射和内核映射不应被清除，所以，仿照这函数，我们需要实现一个专门清除内核页表的函数，位于提交#d7c29f8中：

```c
// Tolerantly free the page table structure,

// for freewalk, it frees the leaf's page,

// this will only free page table itself

void kvmfree(pagetable_t kpagetable)

{

  for (int i = 0; i < PGSIZE / sizeof(pte_t); i++)

  {

    pte_t pte = kpagetable[i];

    if ((pte & PTE_V) && ((pte & (PTE_R | PTE_W | PTE_X)) == 0))

    {

      // this PTE points to a lower-level page table.

      kvmfree((pagetable_t)PTE2PA(pte));

      kpagetable[i] = 0;

    }

  }

  // Release kernel pagetable itself

  kfree((void *)kpagetable);

}
```

## 9. 释放进程，顺带释放页表

这步导致了一个让我调试4个多小时未果的沟槽bug。我们要做的是，在释放进程时释放内核页表。

我们最初的实现如下：

```c
// free a proc structure and the data hanging from it,

// including user pages.

// p->lock must be held.

static void

freeproc(struct proc *p)

{

  if (p->trapframe)

    kfree((void *)p->trapframe);

  p->trapframe = 0;

  if (p->pagetable)

    proc_freepagetable(p->pagetable, p->sz);
    
  // HERE
  if (p->kpagetable)

  {

    // free kernel stack

    uint64 stack_pa = kvmpa(p->kstack);

    kfree((void *)stack_pa);

    // free kernel page table

    kvmfree(p->kpagetable);

  }

  

  p->pagetable = 0;

  p->sz = 0;

  p->pid = 0;

  p->parent = 0;

  p->name[0] = 0;

  p->chan = 0;

  p->killed = 0;

  p->xstate = 0;

  p->state = UNUSED;

}
```

这埋下了一个非常蠢蛋的Bug，我们的逻辑是，借助p->kstack中存放的内核栈地址找到对应的物理地址，直接用kfree释放它：

```c
// free kernel stack

    uint64 stack_pa = kvmpa(p->kstack);

    kfree((void *)stack_pa);
```

我忽略了一个重要的事实：

**清理进程一般是由父进程完成的，而非当前要清理的这个进程。**

kvmpa中使用的父进程的内核页，而p->kstack存放的是子进程的内核栈地址。

这就导致我的kvmpa总是报panic，我用GDB调试到快早上，却总是发现kvmpa的寻址与实际内核页里映射的地址差了两页。这是我耗时最长的一个蠢蛋Bug。

我们其实有一个释放的函数，叫做uvmunmap，它本来是用来释放用户页表的内容的，但这里拿来用也未尝不可。

五个小时后，我解决了它：

```c
  if (p->kpagetable)

  {

    // free the kernel stack in the RAM

    uvmunmap(p->kpagetable, p->kstack, 1, 1);

    // free kernel page table

    kvmfree(p->kpagetable);

  }

  p->kpagetable = 0;
```

至此，本章结束。