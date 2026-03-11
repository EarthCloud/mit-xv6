任务2要求我们不加调试地实现懒分配，具体来说，当陷入发生时，我们应该通过读取scause寄存器的内容，定位到Page Fault的分支，在这里，我们调用的是uvmalloc函数，它能让大多数程序正确运行：

```c
  } else if(r_scause() == 13 || r_scause() == 15) {

    uint64 addr = r_stval();

    if(addr > p->sz) {

      printf("usertrap(): unexpected scause %p pid=%d\n", r_scause(), p->pid);

      printf("            sepc=%p stval=%p\n", r_sepc(), r_stval());

      p->killed = 1;

    }

    uvmalloc(p->pagetable, PGROUNDDOWN(addr), addr);

  }

```

同时，由于已经发生了懒分配，在使用uvmunmap时，当页表项为空时，我们应该只是单纯跳过它，如下：

```c
void

uvmunmap(pagetable_t pagetable, uint64 va, uint64 npages, int do_free)

{

  uint64 a;

  pte_t *pte;

  

  if((va % PGSIZE) != 0)

    panic("uvmunmap: not aligned");

  

  for(a = va; a < va + npages * PGSIZE; a += PGSIZE) {

    if((pte = walk(pagetable, a, 0)) == 0)

     continue;

    if((*pte & PTE_V) == 0)

      continue;

    if(PTE_FLAGS(*pte) == PTE_V)

      panic("uvmunmap: not a leaf");

    if(do_free) {

      uint64 pa = PTE2PA(*pte);

      kfree((void *)pa);

    }

    *pte = 0;

  }

}
```