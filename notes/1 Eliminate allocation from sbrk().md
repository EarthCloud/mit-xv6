第一个任务的要求十分简单：删除系统调用sbrk中分配内存的过程，只增加该进程的sz元数据的值，实现很容易，找到位于sysproc.c中的sys_sbrk入口函数，删除用于分配的growproc函数：

```c
uint64

sys_sbrk(void)

{

  int addr;

  int n;

  

  if(argint(0, &n) < 0)

    return -1;

  addr = myproc()->sz;

  myproc()->sz += n;

  // To achieve lazy allocation, cancel the allocation here

  // if(growproc(n) < 0)

  //   return -1;

  return addr;

}
```