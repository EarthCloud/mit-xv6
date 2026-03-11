我们现在需要彻底完善实现的懒分配机制，使其可以通过所有压力测试。

## 1. 解决n为负数的情况

当sbrk传入的参数为负数时，我们应该立即取消该位置的分配，而不延迟解决，所以，就像growproc中做的那样，当n为负数时，简单地立刻执行uvmdealloc即可：

```c
uint64

sys_sbrk(void)

{

  int          addr;

  int          n;

  struct proc *p = myproc();

  

  if(argint(0, &n) < 0)

    return -1;

  addr = p->sz;

  p->sz += n;

  // If n<0, dealloc immediately

  if(n < 0)

    p->sz = uvmdealloc(p->pagetable, addr, addr + n);

  // To achieve lazy allocation, cancel the allocation here

  // if(growproc(n) < 0)

  //   return -1;

  return addr;

}
```

## 2. 创建新的懒分配函数

使用原先的uvmalloc并不是一个较优的选择，它采用遍历的方式从旧地址到新地址，这就导致，对于某些特殊的测试（比如地址恰好是整页），该函数会导致循环调用。同时，这之中mappages配置的权限为可执行，堆内存数据不应该可执行，这违反了最小权限的要求，会导致漏洞。

综合考虑到上面这些因素，我单独实现了一个lazy_uvmalloc函数，它会合理的处理异常情况，并符合懒分配单页分配的逻辑：

```c
// When lazy allocation cause page fault,

// call this function to alloc one page.

// Return 0 when allocation fail

// pa when allocation succeed,

uint64

lazy_uvmalloc(pagetable_t pagetable, uint64 va)

{

  struct proc *p = myproc();
 // 注意这个判断，它决定了是应该懒分配还是单纯的缺页错误
  if(va >= p->sz || va < p->trapframe->sp) {

    return 0;

  }

  // allocate this page

  uint64 fault_addr = PGROUNDDOWN(va);

  char  *mem;

  if((mem = kalloc()) == 0)

    return 0;

  memset(mem, 0, PGSIZE);

  if(mappages(pagetable, fault_addr, PGSIZE, (uint64)mem,

              PTE_W | PTE_R | PTE_U) != 0) {

    kfree(mem);

    return 0;

  }

  return (uint64)mem;

}
```

## 3. 引入新的懒分配函数

在trap.c中的usertrap()里，引用新的懒分配函数，当分配错误就杀掉进程：

```c
  } else if(r_scause() == 13 || r_scause() == 15) {

    if(lazy_uvmalloc(p->pagetable, r_stval()) == 0) {

      p->killed = 1;

    }
  }
```

同时，内核的某些操作（如copyin/copyout）会使用walkaddr函数，我们应该判读这个虚拟地址是否是懒分配的，因此，在vm.c的walkaddr中同样引入我们的新函数：

```c
// Look up a virtual address, return the physical address,

// or 0 if not mapped.

// Can only be used to look up user pages.

uint64

walkaddr(pagetable_t pagetable, uint64 va)

{

  pte_t *pte;

  uint64 pa;

  

  if(va >= MAXVA)

    return 0;

  pte = walk(pagetable, va, 0);

  if((pte == 0) || (*pte & PTE_V) == 0) {

    if((pa = lazy_uvmalloc(pagetable, va)) == 0)

      return 0;

    else

      return pa;

  }

  if((*pte & PTE_U) == 0)

    return 0;

  pa = PTE2PA(*pte);

  return pa;

}
```

## 4. 解决不应该发生的恐慌

在原来的xv6中，某些函数检测到缺页会直接panic，但考虑到我们已经实现了懒分配，某些缺页也许只是因为为进行的懒分配。

fork使用的uvmcopy是一个例子，在实现了懒分配的内核中，应直接跳过不存在的pte条目，故修改uvmcopy如下：

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

  uint   flags;

  char  *mem;

  

  for(i = 0; i < sz; i += PGSIZE) {

    if((pte = walk(old, i, 0)) == 0)

      continue;

    if((*pte & PTE_V) == 0)

      continue;

    pa    = PTE2PA(*pte);

    flags = PTE_FLAGS(*pte);

    if((mem = kalloc()) == 0)

      goto err;

    memmove(mem, (char *)pa, PGSIZE);

    if(mappages(new, i, PGSIZE, (uint64)mem, flags) != 0) {

      kfree(mem);

      goto err;

    }

  }

  return 0;

  

err:

  uvmunmap(new, 0, i / PGSIZE, 1);

  return -1;

}
```

我们在uvmunmap中也应实现类似的操作，就像我们在任务2中所做的那样。

## 5. 通过autograder

随后执行make grade，可以看到通过了所有测试：

```text
$ make qemu-gdb
(6.7s) 
== Test   lazy: map == 
  lazy: map: OK 
== Test   lazy: unmap == 
  lazy: unmap: OK 
== Test usertests == 
$ make qemu-gdb
(50.1s) 
== Test   usertests: pgbug == 
  usertests: pgbug: OK 
== Test   usertests: sbrkbugs == 
  usertests: sbrkbugs: OK 
== Test   usertests: argptest == 
  usertests: argptest: OK 
== Test   usertests: sbrkmuch == 
  usertests: sbrkmuch: OK 
== Test   usertests: sbrkfail == 
  usertests: sbrkfail: OK 
== Test   usertests: sbrkarg == 
  usertests: sbrkarg: OK 
== Test   usertests: stacktest == 
  usertests: stacktest: OK 
== Test   usertests: execout == 
  usertests: execout: OK 
== Test   usertests: copyin == 
  usertests: copyin: OK 
== Test   usertests: copyout == 
  usertests: copyout: OK 
== Test   usertests: copyinstr1 == 
  usertests: copyinstr1: OK 
== Test   usertests: copyinstr2 == 
  usertests: copyinstr2: OK 
== Test   usertests: copyinstr3 == 
  usertests: copyinstr3: OK 
== Test   usertests: rwsbrk == 
  usertests: rwsbrk: OK 
== Test   usertests: truncate1 == 
  usertests: truncate1: OK 
== Test   usertests: truncate2 == 
  usertests: truncate2: OK 
== Test   usertests: truncate3 == 
  usertests: truncate3: OK 
== Test   usertests: reparent2 == 
  usertests: reparent2: OK 
== Test   usertests: badarg == 
  usertests: badarg: OK 
== Test   usertests: reparent == 
  usertests: reparent: OK 
== Test   usertests: twochildren == 
  usertests: twochildren: OK 
== Test   usertests: forkfork == 
  usertests: forkfork: OK 
== Test   usertests: forkforkfork == 
  usertests: forkforkfork: OK 
== Test   usertests: createdelete == 
  usertests: createdelete: OK 
== Test   usertests: linkunlink == 
  usertests: linkunlink: OK 
== Test   usertests: linktest == 
  usertests: linktest: OK 
== Test   usertests: unlinkread == 
  usertests: unlinkread: OK 
== Test   usertests: concreate == 
  usertests: concreate: OK 
== Test   usertests: subdir == 
  usertests: subdir: OK 
== Test   usertests: fourfiles == 
  usertests: fourfiles: OK 
== Test   usertests: sharedfd == 
  usertests: sharedfd: OK 
== Test   usertests: exectest == 
  usertests: exectest: OK 
== Test   usertests: bigargtest == 
  usertests: bigargtest: OK 
== Test   usertests: bigwrite == 
  usertests: bigwrite: OK 
== Test   usertests: bsstest == 
  usertests: bsstest: OK 
== Test   usertests: sbrkbasic == 
  usertests: sbrkbasic: OK 
== Test   usertests: kernmem == 
  usertests: kernmem: OK 
== Test   usertests: validatetest == 
  usertests: validatetest: OK 
== Test   usertests: opentest == 
  usertests: opentest: OK 
== Test   usertests: writetest == 
  usertests: writetest: OK 
== Test   usertests: writebig == 
  usertests: writebig: OK 
== Test   usertests: createtest == 
  usertests: createtest: OK 
== Test   usertests: openiput == 
  usertests: openiput: OK 
== Test   usertests: exitiput == 
  usertests: exitiput: OK 
== Test   usertests: iput == 
  usertests: iput: OK 
== Test   usertests: mem == 
  usertests: mem: OK 
== Test   usertests: pipe1 == 
  usertests: pipe1: OK 
== Test   usertests: preempt == 
  usertests: preempt: OK 
== Test   usertests: exitwait == 
  usertests: exitwait: OK 
== Test   usertests: rmdot == 
  usertests: rmdot: OK 
== Test   usertests: fourteen == 
  usertests: fourteen: OK 
== Test   usertests: bigfile == 
  usertests: bigfile: OK 
== Test   usertests: dirfile == 
  usertests: dirfile: OK 
== Test   usertests: iref == 
  usertests: iref: OK 
== Test   usertests: forktest == 
  usertests: forktest: OK 
== Test time == 
time: OK 
Score: 119/119
```