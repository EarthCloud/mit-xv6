## 1. 概览

Alarm实验要求我们实现两个系统调用，以便根据CPU经过的时钟滴答次数，有频率地注入一些逻辑。sigalarm负责设置间隔和注入函数，而sigreturn让我们从我们注入的函数中返回，保证调用alarm_handler的透明性。

## 2. 实现过程

### 1. 注册系统调用存根

老生常谈，不再赘述。

### 2. 添加新字段

在proc.h中添加新字段以满足我们要求：

```c
// Per-process state

struct proc {

  struct spinlock lock;
  
// ......

  int               alarm_ticks;   // Ticks after last alarm call

  uint64            interval;      // Alarm intervals

  int               is_in_handler; // Flag to check if it's in handler

  uint64            handler;       // Alarm handler

  char              name[16];      // Process name (debugging)

};
```

注意`is_in_handler`字段，它标识此时是否进入了handler，如果进入，我们不应该重入该函数；同时，handler直接设置为uint64形式即可，因为pc归根结底也是存了一个地址。

注意：你需要在allocproc和freeproc中实现相关字段的初始化和销毁。

### 3. 实现sigalarm系统调用

我们的sigalarm需要保存状态：

```c
uint64

sys_sigalarm(void)

{

  int interval;

  uint64 handler;

  struct proc *p        = myproc();

  if(argint(0, &interval) < 0)

    return -1;

  if(argaddr(1, &handler) < 0)

    return -1;

  if(interval < 0) {

    return -1;

  }

  p->interval = interval, p->handler = handler;

  return 0;

}
```

### 4. 当其为时钟中断时进行检查和累加

在trap.c中，每当进入时钟中断，就累加并判断是否进入handler
```c
  if(which_dev == 2) {

    if(++(p->alarm_ticks) == p->interval && !p->is_in_handler && p->interval) {

      p->trapframe->epc = p->handler;

      p->alarm_ticks    = 0;

      p->is_in_handler  = 1;

    }

    yield();

  }
```

### 5. 实现透明性

如果单纯设置sigalarm，那么在返回时，程序的寄存器很有可能被handler修改，因而在后续执行其他语句时会出现异常。所以，我们需要对寄存器进行备份。

一个简单的思路是直接添加一个新的备份陷阱帧，我们的陷阱帧保存了陷入时的所有寄存器内容，因此节省了很多操作。该页不需要被映射到用户地址空间，它被内核保留，我们只需要在初始化进程时为其分配一页，并在释放时销毁即可。

### 6. 添加backup字段并初始化

在proc.h的结构体中添加新字段

```c
struct proc{
// ......
	struct trapframe *backup_trapframe; // Back up trapframe for alarm
}
```

进行初始化和销毁工作

```c
// in proc.c allocproc()
  if((p->backup_trapframe = (struct trapframe *)kalloc()) == 0) {

    release(&p->lock);

    return 0;

  }
  
  // in proc.c freeproc()
    if(p->backup_trapframe)

    kfree((void *)p->backup_trapframe);

  p->backup_trapframe = 0;
  
```

### 7. 实现复制

在trap.c中，只需使用memmove复制即可：

```c
if(which_dev == 2) {

    if(++(p->alarm_ticks) == p->interval && !p->is_in_handler && p->interval) {

      memmove(p->backup_trapframe, p->trapframe, sizeof(struct trapframe));

      p->trapframe->epc = p->handler;

      p->alarm_ticks    = 0;

      p->is_in_handler  = 1;

    }

    yield();

  }
```

在返回时把backup中的内容复制回来即可：

```c
uint64

sys_sigreturn(void)

{

  struct proc *p = myproc();

  // copy back trapframe, keep transparency

  memmove(p->trapframe, p->backup_trapframe, sizeof(struct trapframe));

  p->is_in_handler = 0;

  p->alarm_ticks   = 0;

  return p->trapframe->a0;

}
```

这里有两个注意的点：
1. 在handler中时，**不需要**统计滴答数，所以在返回时要把滴答置0
2.  要返回原来a0中存储的内容，以保证透明性。

## 3. 通过

以此，我们成功通过了Autograder

```
== Test answers-traps.txt == answers-traps.txt: OK 
== Test backtrace test == 
$ make qemu-gdb
backtrace test: OK (6.6s) 
== Test running alarmtest == 
$ make qemu-gdb
(3.3s) 
== Test   alarmtest: test0 == 
  alarmtest: test0: OK 
== Test   alarmtest: test1 == 
  alarmtest: test1: OK 
== Test   alarmtest: test2 == 
  alarmtest: test2: OK 
== Test usertests == 
$ make qemu-gdb
usertests: OK (50.4s) 
== Test time == 
time: OK 
Score: 85/85
```
