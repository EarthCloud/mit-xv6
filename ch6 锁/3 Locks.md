## 6. xv6中的锁

Xv6有两种类型的锁：自旋锁（spinlocks）和睡眠锁（sleep-locks）。

我们将从自旋锁（注：自旋，即循环等待）开始。Xv6将自旋锁表示为`struct spinlock` (**_kernel/spinlock.h_**:2)。

```c
// Mutual exclusion lock.

struct spinlock {

  uint locked; // Is the lock held?

  

  // For debugging:

  char       *name; // Name of lock.

  struct cpu *cpu;  // The cpu holding the lock.

};
```

结构体中的重要字段是`locked`，当锁可用时为零，当它被持有时为非零。从逻辑上讲，xv6应该通过执行以下代码来获取锁。

```c
void
acquire(struct spinlock* lk) // does not work!
{
  for(;;) {
    if(lk->locked == 0) {
      lk->locked = 1;
      break;
    }
  }
}
```

不幸的是，这种实现**不能**保证多处理器上的互斥。可能会发生两个CPU同时到达第5行，看到`lk->locked`为零，然后都通过执行第6行占有锁。此时就有两个不同的CPU持有锁，从而违反了互斥属性。我们需要的是一种方法，使第5行和第6行作为原子（即不可分割）步骤执行。

## 7. 锁的硬件实现

因为锁被广泛使用，多核处理器通常提供实现第5行和第6行的原子版本的指令。

在RISC-V上，这条指令是`amoswap r, a`。`amoswap`读取内存地址`a`处的值，将寄存器`r`的内容写入该地址，并将其读取的值放入`r`中。也就是说，它交换寄存器和指定内存地址的内容。

它原子地执行这个指令序列，使用特殊的硬件来防止任何其他CPU在读取和写入之间使用内存地址。

## 8. 锁的软件实现

Xv6的`acquire`(**_kernel/spinlock.c_**:22)使用可移植的C库调用归结为`amoswap`的指令`__sync_lock_test_and_set`。返回值是`lk->locked`的旧（交换了的）内容。`acquire`函数将swap包装在一个循环中，直到它获得了锁前一直重试（自旋）。每次迭代将1与`lk->locked`进行swap操作，并检查`lk->locked`之前的值。如果之前为0，swap已经把`lk->locked`设置为1，那么我们就获得了锁；如果前一个值是1，那么另一个CPU持有锁，我们原子地将1与`lk->locked`进行swap的事实并没有改变它的值。

```c
// Acquire the lock.

// Loops (spins) until the lock is acquired.

void

acquire(struct spinlock *lk)

{

  push_off(); // disable interrupts to avoid deadlock.

  if(holding(lk))

    panic("acquire");

  // On RISC-V, sync_lock_test_and_set turns into an atomic swap:

  //   a5 = 1

  //   s1 = &lk->locked

  //   amoswap.w.aq a5, a5, (s1)

  while(__sync_lock_test_and_set(&lk->locked, 1) != 0)

    ;

  

  // Tell the C compiler and the processor to not move loads or stores

  // past this point, to ensure that the critical section's memory

  // references happen strictly after the lock is acquired.

  // On RISC-V, this emits a fence instruction.

  __sync_synchronize();

  

  // Record info about lock acquisition for holding() and debugging.

  lk->cpu = mycpu();

}
```

获取锁后，用于调试，`acquire`将记录下来获取锁的CPU。`lk->cpu`字段受锁保护，只能在保持锁时更改。

函数`release`(**_kernel/spinlock.c_**:47) 与`acquire`相反：它清除`lk->cpu`字段，然后释放锁。从概念上讲，`release`只需要将0分配给`lk->locked`。C标准允许编译器用多个存储指令实现赋值，因此对于并发代码，C赋值可能是非原子的。因此`release`使用执行原子赋值的C库函数`__sync_lock_release`。该函数也可以归结为RISC-V的`amoswap`指令。

```c
// Release the lock.

void

release(struct spinlock *lk)

{

  if(!holding(lk))

    panic("release");

  

  lk->cpu = 0;

  

  // Tell the C compiler and the CPU to not move loads or stores

  // past this point, to ensure that all the stores in the critical

  // section are visible to other CPUs before the lock is released,

  // and that loads in the critical section occur strictly before

  // the lock is released.

  // On RISC-V, this emits a fence instruction.

  __sync_synchronize();

  

  // Release the lock, equivalent to lk->locked = 0.

  // This code doesn't use a C assignment, since the C standard

  // implies that an assignment might be implemented with

  // multiple store instructions.

  // On RISC-V, sync_lock_release turns into an atomic swap:

  //   s1 = &lk->locked

  //   amoswap.w zero, zero, (s1)

  __sync_lock_release(&lk->locked);

  

  pop_off();

}
```