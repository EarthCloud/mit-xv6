## 13. 介绍exec的实现

`exec`是创建地址空间的用户部分的系统调用。它使用一个存储在文件系统中的文件初始化地址空间的用户部分。`exec`(**_kernel/exec.c_**:13)使用`namei` (**_kernel/exec.c_**:26)打开指定的二进制`path`，这在第8章中有解释。然后，它读取ELF头。Xv6应用程序以广泛使用的ELF格式描述，定义于(**_kernel/elf.h_**)。ELF二进制文件由ELF头、`struct elfhdr`(**_kernel/elf.h_**:6)，后面一系列的程序节头（section headers）、`struct proghdr`(**_kernel/elf.h_**:25)组成。每个`proghdr`描述程序中必须加载到内存中的一节（section）；xv6程序只有一个程序节头，但是其他系统对于指令和数据部分可能各有单独的节。

## 14. 执行过程

我们详解exec系统调用的实现，它位于`kexec()`中。

第一步是快速检查文件可能包含ELF二进制的文件。ELF二进制文件以四个字节的“幻数”`0x7F`、“`E`”、“`L`”、“`F`”或`ELF_MAGIC`开始(**_kernel/elf.h_**:3)。如果ELF头有正确的幻数，`exec`假设二进制文件格式良好，幻数亦译作魔数。x86 和 RISC-V 都是小端序（Little Endian）架构。内存中（低地址 -> 高地址）：`7F 45 4C 46`，读取为 32 位整数（`uint`）时：低地址是低位，高地址是高位，所以数值解析为 `0x464C457F`。

```c
  

  // Read the ELF header.

  if(readi(ip, 0, (uint64)&elf, 0, sizeof(elf)) != sizeof(elf))

    goto bad;
```

`bad`标签是事务崩溃的统一处理逻辑，定义在方法体的最后，它主要负责释放页表和结束事务：

```c
bad:

  if(pagetable)

    proc_freepagetable(pagetable, sz);

  if(ip){

    iunlockput(ip);

    end_op();

  }

  return -1;
```

`exec`使用`proc_pagetable` (**_kernel/exec.c_**:38)分配一个没有用户映射的新页表，使用`uvmalloc` (**_kernel/exec.c_**:52)为每个ELF段分配内存，并使用`loadseg` (**_kernel/exec.c_**:10)将每个段加载到内存中。`loadseg`使用`walkaddr`找到分配内存的物理地址，在该地址写入ELF段的每一页，并使用`readi`从文件中读取。

```c
if((pagetable = proc_pagetable(p)) == 0)

    goto bad;
  
  // Load program into memory.

  for(i=0, off=elf.phoff; i<elf.phnum; i++, off+=sizeof(ph)){

    if(readi(ip, 0, (uint64)&ph, off, sizeof(ph)) != sizeof(ph))

      goto bad;

    if(ph.type != ELF_PROG_LOAD)

      continue;

    if(ph.memsz < ph.filesz)

      goto bad;

    if(ph.vaddr + ph.memsz < ph.vaddr)

      goto bad;

    if(ph.vaddr % PGSIZE != 0)

      goto bad;

    uint64 sz1;

    if((sz1 = uvmalloc(pagetable, sz, ph.vaddr + ph.memsz, flags2perm(ph.flags))) == 0)

      goto bad;

    sz = sz1;

    if(loadseg(pagetable, ph.vaddr, ip, ph.off, ph.filesz) < 0)

      goto bad;

  }

  iunlockput(ip);

  end_op();
```

现在`exec`分配并初始化用户栈。它只分配一个栈页面。`exec`一次将参数中的一个字符串复制到栈顶，并在`ustack`中记录指向它们的指针。它在传递给`main`的`argv`列表的末尾放置一个空指针。`ustack`中的前三个条目是伪返回程序计数器（fake return program counter）、`argc`和`argv`指针。

```c
  p = myproc();

  uint64 oldsz = p->sz;

  

  // Allocate two pages at the next page boundary.

  // Use the second as the user stack.

  sz = PGROUNDUP(sz);

  uint64 sz1;

  if((sz1 = uvmalloc(pagetable, sz, sz + 2*PGSIZE)) == 0)

    goto bad;

  sz = sz1;

  uvmclear(pagetable, sz-2*PGSIZE);

  sp = sz;

  stackbase = sp - PGSIZE;

  

  // Push argument strings, prepare rest of stack in ustack.

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

  

  // push the array of argv[] pointers.

  sp -= (argc+1) * sizeof(uint64);

  sp -= sp % 16;

  if(sp < stackbase)

    goto bad;

  if(copyout(pagetable, sp, (char *)ustack, (argc+1)*sizeof(uint64)) < 0)

    goto bad;

  

  // arguments to user main(argc, argv)

  // argc is returned via the system call return

  // value, which goes in a0.

  p->trapframe->a1 = sp;

  

  // Save program name for debugging.

  for(last=s=path; *s; s++)

    if(*s == '/')

      last = s+1;

  safestrcpy(p->name, last, sizeof(p->name));
```


`exec`在栈页面的正下方放置了一个不可访问的页面（即Guard Page），这样试图使用超过一个页面的程序就会出错。这个不可访问的页面还允许`exec`处理过大的参数；在这种情况下，被`exec`用来将参数复制到栈的函数`copyout`(**_kernel/vm.c_**:355) 将会注意到目标页面不可访问，并返回-1。


在准备新内存映像的过程中，如果`exec`检测到像无效程序段这样的错误，它会跳到标签`bad`，释放新映像，并返回-1。`exec`必须等待系统调用会成功后再释放旧映像：因为如果旧映像消失了，系统调用将无法返回-1。`exec`中唯一的错误情况发生在映像的创建过程中。一旦映像完成，`exec`就可以提交到新的页表(**_kernel/exec.c_**:113)并释放旧的页表(**_kernel/exec.c_**:117)。

```c
  // Commit to the user image.

  oldpagetable = p->pagetable;

  p->pagetable = pagetable;

  p->sz = sz;

  p->trapframe->epc = elf.entry;  // initial program counter = main

  p->trapframe->sp = sp; // initial stack pointer

  proc_freepagetable(oldpagetable, oldsz);
```

## 15. exec的检查

`exec`将ELF文件中的字节加载到ELF文件指定地址的内存中。用户或进程可以将他们想要的任何地址放入ELF文件中。因此`exec`是有风险的，因为ELF文件中的地址可能会意外或故意的引用内核。对一个设计拙劣的内核来说，后果可能是一次崩溃，甚至是内核的隔离机制被恶意破坏（即安全漏洞）。xv6执行许多检查来避免这些风险。例如，`if(ph.vaddr + ph.memsz < ph.vaddr)`检查总和是否溢出64位整数，危险在于用户可能会构造一个ELF二进制文件，其中的`ph.vaddr`指向用户选择的地址，而`ph.memsz`足够大，使总和溢出到0x1000，这看起来像是一个有效的值。在xv6的旧版本中，用户地址空间也包含内核（但在用户模式下不可读写），用户可以选择一个与内核内存相对应的地址，从而将ELF二进制文件中的数据复制到内核中。在xv6的RISC-V版本中，这是不可能的，因为内核有自己独立的页表；`loadseg`加载到进程的页表中，而不是内核的页表中。

内核开发人员很容易省略关键的检查，而现实世界中的内核有很长一段丢失检查的历史，用户程序可以利用这些检查的缺失来获得内核特权。xv6可能没有完成验证提供给内核的用户级数据的全部工作，恶意用户程序可以利用这些数据来绕过xv6的隔离。
