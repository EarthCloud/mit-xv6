## 16. 适用于sleep和wakeup调用的例子

`Sleep`和`wakeup`可用于多种等待。

### a. wait

第一章介绍的一个有趣的例子是子进程`exit`和父进程`wait`之间的交互。在子进程死亡时，父进程可能已经在`wait`中休眠，或者正在做其他事情；在后一种情况下，随后的`wait`调用必须观察到子进程的死亡，可能是在子进程调用`exit`后很久。xv6记录子进程终止直到`wait`观察到它的方式是让`exit`将调用方置于`ZOMBIE`状态，在那里它一直保持到父进程的`wait`注意到它，将子进程的状态更改为`UNUSED`，复制子进程的`exit`状态码，并将子进程ID返回给父进程。如果父进程在子进程之前退出，则父进程将子进程交给`init`进程，`init`进程将永久调用`wait`；因此，每个子进程退出后都有一个父进程进行清理。主要的实现挑战是父级和子级`wait`和`exit`，以及`exit`和`exit`之间可能存在竞争和死锁。

`Wait`使用调用进程的`p->lock`作为条件锁，以避免丢失唤醒，并在开始时获取该锁（**_kernel/proc.c_**:398）。

```c
// Wait for a child process to exit and return its pid.

// Return -1 if this process has no children.

int

wait(uint64 addr)

{

  struct proc *np;

  int          havekids, pid;

  struct proc *p = myproc();

  

  // hold p->lock for the whole time to avoid lost

  // wakeups from a child's exit().

  acquire(&p->lock);

}
```

然后它扫描进程表。如果它发现一个子进程处于`ZOMBIE`状态，它将释放该子进程的资源及其`proc`结构体，将该子进程的退出状态码复制到提供给`wait`的地址（如果不是0），并返回该子进程的进程ID。如果`wait`找到子进程但没有子进程退出，它将调用`sleep`以等待其中一个退出（**_kernel/proc.c_**:445），然后再次扫描。

```c
  for(;;) {
    // Scan through table looking for exited children.
    havekids = 0;

    for(np = proc; np < &proc[NPROC]; np++) {

      // this code uses np->parent without holding np->lock.

      // acquiring the lock first would cause a deadlock,

      // since np might be an ancestor, and we already hold p->lock.
      if(np->parent == p) {
        // np->parent can't change between the check and the acquire()

        // because only the parent changes it, and we're the parent.
        acquire(&np->lock);
        havekids = 1;
        if(np->state == ZOMBIE) {
          // Found one.
          pid = np->pid;
          if(addr != 0 && copyout(p->pagetable, addr, (char *)&np->xstate,sizeof(np->xstate)) < 0) {
            release(&np->lock);
            release(&p->lock);
            return -1;

          }
          freeproc(np);
          release(&np->lock);
          release(&p->lock);
          return pid;

        }
        release(&np->lock);
      }
   }
     // No point waiting if we don't have any children.

    if(!havekids || p->killed) {

      release(&p->lock);

      return -1;

    }

  

    // Wait for a child to exit.

    sleep(p, &p->lock); // DOC: wait-sleep

  }
```

这里，`sleep`中释放的条件锁是等待进程的`p->lock`，这是上面提到的特例。注意，`wait`通常持有两个锁：它在试图获得任何子进程的锁之前先获得自己的锁；因此，整个xv6都必须遵守相同的锁定顺序（父级，然后是子级），以避免死锁。

`Wait`查看每个进程的`np->parent`以查找其子进程。它使用`np->parent`而不持有`np->lock`，这违反了通常的规则，即共享变量必须受到锁的保护。`np`可能是当前进程的祖先，在这种情况下，获取`np->lock`可能会导致死锁，因为这将违反上述顺序。这种情况下无锁检查`np->parent`似乎是安全的：进程的`parent`字段仅由其父进程更改，因此如果`np->parent==p`为`true`，除非当前流程更改它，否则该值无法被更改。

### b. exit

`Exit`（**_kernel/proc.c_**:333）记录退出状态码，释放一些资源，将所有子进程提供给`init`进程，在父进程处于等待状态时唤醒父进程，将调用方标记为僵尸进程（zombie），并永久地让出CPU。最后的顺序有点棘手。退出进程必须在将其状态设置为`ZOMBIE`并唤醒父进程时持有其父进程的锁，因为父进程的锁是防止在`wait`中丢失唤醒的条件锁。子级还必须持有自己的`p->lock`，否则父级可能会看到它处于`ZOMBIE`状态，并在它仍运行时释放它。锁获取顺序对于避免死锁很重要：因为`wait`先获取父锁再获取子锁，所以`exit`必须使用相同的顺序。

```c
// Exit the current process.  Does not return.
// An exited process remains in the zombie state
// until its parent calls wait().
void
exit(int status)
{
  struct proc *p = myproc();

  if(p == initproc)
    panic("init exiting");

  // Close all open files.
  for(int fd = 0; fd < NOFILE; fd++) {
    if(p->ofile[fd]) {
      struct file *f = p->ofile[fd];
      fileclose(f);
      p->ofile[fd] = 0;
    }
  }

  begin_op();
  iput(p->cwd);
  end_op();
  p->cwd = 0;

  // we might re-parent a child to init. we can't be precise about
  // waking up init, since we can't acquire its lock once we've
  // acquired any other proc lock. so wake up init whether that's
  // necessary or not. init may miss this wakeup, but that seems
  // harmless.
  acquire(&initproc->lock);
  wakeup1(initproc);
  release(&initproc->lock);

  // grab a copy of p->parent, to ensure that we unlock the same
  // parent we locked. in case our parent gives us away to init while
  // we're waiting for the parent lock. we may then race with an
  // exiting parent, but the result will be a harmless spurious wakeup
  // to a dead or wrong process; proc structs are never re-allocated
  // as anything else.
  acquire(&p->lock);
  struct proc *original_parent = p->parent;
  release(&p->lock);

  // we need the parent's lock in order to wake it up from wait().
  // the parent-then-child rule says we have to lock it first.
  acquire(&original_parent->lock);

  acquire(&p->lock);

  // Give any children to init.
  reparent(p);

  // Parent might be sleeping in wait().
  wakeup1(original_parent);

  p->xstate = status;
  p->state  = ZOMBIE;

  release(&original_parent->lock);

  // Jump into the scheduler, never to return.
  sched();
  panic("zombie exit");
}
```

`Exit`调用一个专门的唤醒函数`wakeup1`，该函数仅唤醒父进程，且父进程必须正在`wait`中休眠（**_kernel/proc.c_**:598）。

在将自身状态设置为`ZOMBIE`之前，子进程唤醒父进程可能看起来不正确，但这是安全的：虽然`wakeup1`可能会导致父进程运行，但`wait`中的循环在`scheduler`释放子进程的`p->lock`之前无法检查子进程，所以`wait`在`exit`将其状态设置为`ZOMBIE`（kernel/proc.c:386）之前不能查看退出进程。

### c. kill

```c
// Kill the process with the given pid.

// The victim won't exit until it tries to return

// to user space (see usertrap() in trap.c).

int

kill(int pid)

{

  struct proc *p;

  

  for(p = proc; p < &proc[NPROC]; p++) {

    acquire(&p->lock);

    if(p->pid == pid) {

      p->killed = 1;

      if(p->state == SLEEPING) {

        // Wake process from sleep().

        p->state = RUNNABLE;

      }

      release(&p->lock);

      return 0;

    }

    release(&p->lock);

  }

  return -1;

}
```

`exit`允许进程自行终止，而`kill`（**_kernel/proc.c_**:611）允许一个进程请求另一个进程终止。对于`kill`来说，直接销毁受害者进程（即要杀死的进程）太复杂了，因为受害者可能在另一个CPU上执行，也许是在更新内核数据结构的敏感序列中间。因此，`kill`的工作量很小：它只是设置受害者的`p->killed`，如果它正在睡眠，则唤醒它。受害者进程终将进入或离开内核，此时，如果设置了`p->killed`，`usertrap`中的代码将调用`exit`。如果受害者在用户空间中运行，它将很快通过进行系统调用或由于计时器（或其他设备）中断而进入内核。

如果受害者进程在`sleep`中，`kill`对`wakeup`的调用将导致受害者从`sleep`中返回。这存在潜在的危险，因为等待的条件可能不为真。但是，xv6对`sleep`的调用总是封装在`while`循环中，该循环在`sleep`返回后重新测试条件。一些对`sleep`的调用还在循环中测试`p->killed`，如果它被设置，则放弃当前活动。只有在这种放弃是正确的情况下才能这样做。例如，如果设置了`killed`标志，则管道读写代码返回；最终代码将返回到陷阱，陷阱将再次检查标志并退出。

一些XV6的`sleep`循环不检查`p->killed`，因为代码在应该是原子操作的多步系统调用的中间。virtio驱动程序（**_kernel/virtio_disk.c_**:242）就是一个例子：它不检查`p->killed`，因为一个磁盘操作可能是文件系统保持正确状态所需的一组写入操作之一。等待磁盘I/O时被杀死的进程将不会退出，直到它完成当前系统调用并且`usertrap`看到`killed`标志。